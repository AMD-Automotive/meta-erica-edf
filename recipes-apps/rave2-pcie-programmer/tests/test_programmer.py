# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

import importlib.util
import hashlib
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock


PROGRAMMER_PATH = Path(__file__).parents[1] / "files" / "programmer.py"
SPEC = importlib.util.spec_from_file_location("rave2_pcie_programmer", PROGRAMMER_PATH)
programmer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(programmer)


class SparseSdaProgrammingTests(unittest.TestCase):
    def run_transfer(self, chunks, total_chunks=4):
        memory = bytearray(4096)
        statuses = []
        chunk_iter = iter(chunks)

        def load_chunk(chunk):
            chunk_number, data = chunk
            memory[programmer.CHUNK_NUMBER_OFFSET:programmer.CHUNK_NUMBER_OFFSET + 4] = struct.pack(
                "<I", chunk_number
            )
            memory[programmer.SIZE_OFFSET:programmer.SIZE_OFFSET + 4] = struct.pack("<I", len(data))

        first_chunk = next(chunk_iter)
        load_chunk(first_chunk)
        memory[programmer.TOTAL_CHUNKS_OFFSET:programmer.TOTAL_CHUNKS_OFFSET + 4] = struct.pack(
            "<I", total_chunks
        )
        memory[programmer.TOTAL_SIZE_LO_OFFSET:programmer.TOTAL_SIZE_LO_OFFSET + 4] = struct.pack(
            "<I", total_chunks * 4
        )

        def validate_payload(_memory, _logger):
            chunk_number = programmer.read_u32(memory, programmer.CHUNK_NUMBER_OFFSET)
            data = dict(chunks)[chunk_number]
            return True, None, data

        def wait_for_signal(_memory, _logger, _signals):
            load_chunk(next(chunk_iter))
            return programmer.SDA_GO_SIGNAL

        def write_u32(_memory, offset, value):
            if offset == programmer.STATUS_OFFSET:
                statuses.append(value)

        with tempfile.TemporaryDirectory() as directory:
            device = Path(directory) / "sda"
            device.touch()
            logger = programmer.LogCapture()
            with mock.patch.object(programmer, "SDA_DEVICE", str(device)), \
                    mock.patch.object(programmer, "SDA_CHUNK_MAX", 4), \
                    mock.patch.object(programmer, "get_sda_capacity", return_value=1024), \
                    mock.patch.object(programmer, "validate_payload", side_effect=validate_payload), \
                    mock.patch.object(programmer, "wait_for_signal", side_effect=wait_for_signal), \
                    mock.patch.object(programmer, "write_u32", side_effect=write_u32):
                result = programmer.handle_sda_programming(memory, logger)
            return result, statuses, device.read_bytes(), logger.get_logs().decode()

    def test_sparse_chunks_write_at_absolute_offsets(self):
        result, statuses, data, _logs = self.run_transfer(
            [(0, b"AAAA"), (2, b"CCCC"), (3, b"DDDD")]
        )
        self.assertEqual(result, programmer.SDA_SUCCESS)
        self.assertEqual(statuses, [programmer.SDA_CHUNK_ACK, programmer.SDA_CHUNK_ACK])
        self.assertEqual(data, b"AAAA" + b"\0" * 4 + b"CCCCDDDD")

    def test_duplicate_chunk_is_rejected(self):
        result, _statuses, _data, logs = self.run_transfer([(0, b"AAAA"), (0, b"BBBB")])
        self.assertEqual(result, programmer.SDA_BAD)
        self.assertIn("is not after previous chunk", logs)

    def test_out_of_range_chunk_is_rejected(self):
        result, _statuses, _data, logs = self.run_transfer([(4, b"AAAA")])
        self.assertEqual(result, programmer.SDA_BAD)
        self.assertIn("outside total chunk count", logs)


class VariableUfsTransferTests(unittest.TestCase):
    class Memory(bytearray):
        def flush(self):
            pass

    def set_u32(self, memory, offset, value):
        memory[offset:offset + 4] = struct.pack("<I", value)

    def load_extent(self, memory, offset, data, final=False):
        self.set_u32(memory, programmer.TRANSFER_OFFSET_LO, offset & 0xFFFFFFFF)
        self.set_u32(memory, programmer.TRANSFER_OFFSET_HI, offset >> 32)
        self.set_u32(memory, programmer.SIZE_OFFSET, len(data))
        self.set_u32(memory, programmer.TRANSFER_FLAGS_OFFSET, programmer.TRANSFER_FLAG_FINAL if final else 0)

    def run_variable_write(self, bad_digest=False):
        memory = bytearray(4096)
        extents = [(0, b"AAAA", False), (12, b"DDDD", True)]
        extent_iter = iter(extents)
        current = [next(extent_iter)]
        self.load_extent(memory, *current[0])
        memory[256:256 + len(current[0][1])] = current[0][1]

        def wait_for_signal(_memory, _logger, _signals):
            if programmer.UFS_VERIFY_GO in _signals:
                digest_a = b"\0" * 32 if bad_digest else hashlib.sha256(b"AAAA").digest()
                manifest = programmer.UFS_MANIFEST_HEADER.pack(
                    programmer.UFS_MANIFEST_MAGIC, programmer.UFS_MANIFEST_VERSION,
                    programmer.UFS_MANIFEST_SHA256, 2,
                ) + programmer.UFS_MANIFEST_RECORD.pack(0, 4, digest_a) \
                    + programmer.UFS_MANIFEST_RECORD.pack(12, 4, hashlib.sha256(b"DDDD").digest())
                memory[256:256 + len(manifest)] = manifest
                self.set_u32(memory, programmer.SIZE_OFFSET, len(manifest))
                return programmer.UFS_VERIFY_GO
            current[0] = next(extent_iter)
            self.load_extent(memory, *current[0])
            memory[256:256 + len(current[0][1])] = current[0][1]
            return programmer.UFS_WRITE_GO

        with tempfile.TemporaryDirectory() as directory:
            device = Path(directory) / "sda"
            device.touch()
            with mock.patch.object(programmer, "SDA_DEVICE", str(device)), \
                    mock.patch.object(programmer, "PAYLOAD_OFFSET", 256), \
                    mock.patch.object(programmer, "get_sda_capacity", return_value=1024), \
                    mock.patch.object(programmer, "wait_for_signal", side_effect=wait_for_signal):
                result = programmer.handle_ufs_write(memory, programmer.LogCapture())
            return result, device.read_bytes()

    def test_variable_write_preserves_gap_and_verifies_manifest(self):
        result, data = self.run_variable_write()
        self.assertEqual(result, programmer.UFS_WRITE_SUCCESS)
        self.assertEqual(data, b"AAAA" + b"\0" * 8 + b"DDDD")

    def test_variable_write_rejects_bad_manifest_checksum(self):
        result, _data = self.run_variable_write(bad_digest=True)
        self.assertEqual(result, programmer.SDA_BAD)

    def test_readback_returns_requested_data_and_md5(self):
        memory = self.Memory(4096)
        self.load_extent(memory, 4, b"data")
        statuses = []

        with tempfile.TemporaryDirectory() as directory:
            device = Path(directory) / "sda"
            device.write_bytes(b"0123456789abcdef")
            original_write_u32 = programmer.write_u32

            def capture_write(_memory, offset, value):
                original_write_u32(_memory, offset, value)
                if offset == programmer.STATUS_OFFSET:
                    statuses.append(value)

            with mock.patch.object(programmer, "SDA_DEVICE", str(device)), \
                    mock.patch.object(programmer, "PAYLOAD_OFFSET", 256), \
                    mock.patch.object(programmer, "get_sda_capacity", return_value=16), \
                    mock.patch.object(programmer, "write_u32", side_effect=capture_write), \
                    mock.patch.object(programmer, "wait_for_signal", return_value=programmer.UFS_READ_DONE):
                result = programmer.handle_ufs_read(memory, programmer.LogCapture())

            self.assertEqual(result, programmer.UFS_READ_SUCCESS)
            self.assertEqual(memory[256:260], b"4567")
            self.assertEqual(statuses, [programmer.UFS_READ_READY])
            md5_words = tuple(
                programmer.read_u32(memory, offset)
                for offset in [programmer.MD5_OFFSET_0, programmer.MD5_OFFSET_1,
                               programmer.MD5_OFFSET_2, programmer.MD5_OFFSET_3]
            )
            self.assertEqual(md5_words, programmer.calculate_md5(b"4567"))


if __name__ == "__main__":
    unittest.main()