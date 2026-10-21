# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Resnet50 VE2 example"
DESCRIPTION = "Resnet50 VE2 example"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Source file
SRC_URI = "https://mkmartifactory.amd.com:8443/artifactory/SW-AutomotiveDevOps-DEV-LOCAL/vaiml/examples/resnet50_model_keep-outputs-false.tar.gz \
                file://0001-Update-modelrun-to-have-central-config-and-cache-dir.patch \
                file://run_example.sh \
        "
SRC_URI[sha256sum] = "28a97423e1958d9b37f211cc2690c53e88427bdd924bb8dca22b2acd46c1f3d0"
S = "${WORKDIR}/resnet50_model"

do_compile() {
        ${CXX} \
        ${CXXFLAGS} \
        -I${STAGING_INCDIR}/onnxruntime/core/session \
        -o model-app.elf \
        -L${STAGING_LIBDIR}/python3.12/site-packages/voe/lib/ \
        -Wl,-O1 \
        -Wl,--hash-style=gnu \
        -Wl,--as-needed \
        -Wl,-z,relro,-z,now \
        ve2_ortCppApi_modelrun.cpp \
        -lonnxruntime 
}

do_install() {
        install -d ${D}${bindir}
        install -m 0755 ${S}/model-app.elf ${D}${bindir}/model-app.elf

        install -d ${D}${datadir}/${PN}/my_cache_dir

        tar --no-same-owner \
        -cpf - my_cache_dir \
        | tar --no-same-owner -xpf - -C ${D}${datadir}/${PN}

        install -m 0644 resnet50_quantized_xint8.onnx ${D}${datadir}/${PN}
        install -m 0644 input0.bin ${D}${datadir}/${PN}
        install -m 0755 ${S}/../run_example.sh ${D}${datadir}/${PN}
	install -m 0644 vitisai_config.json ${D}${datadir}/${PN}

        
        install -d ${D}${libdir}
        install -m 0755 libmlir_c_runner_utils.so ${D}${libdir}
        install -m 0755 libmlir_float16_utils.so ${D}${libdir}
}

DEPENDS += "onnxrt-headers ryzenai-wheels"
RDEPENDS:${PN} += "ryzenai-wheels"

INSANE_SKIP:${PN} += "file-rdeps dev-deps dev-elf"
INSANE_SKIP:${PN}-dev += "file-rdeps dev-deps dev-elf"