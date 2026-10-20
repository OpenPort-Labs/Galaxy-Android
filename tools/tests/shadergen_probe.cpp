// Host build of the GX shader generator for the deterministic source tests in
// test_shadergen.py.  The generator is plain GLSL string building, so the part
// that decides what the vertex shader computes can be checked without a GPU.
//
// usage: shadergen_probe <color-ctrl hex> [alpha-ctrl hex]
//   prints the generated vertex shader for a one-channel material that is lit
//   by GX_LIGHT0 with the given channel control words (XF 0x100E / 0x1010).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "vk_shadergen.h"
#include "gx_shader_uid.h"
#include "gx_record.h"

static uint32_t parseHex(const char* s) {
    return (uint32_t)strtoul(s, nullptr, 0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s [--fs] <color-ctrl> [alpha-ctrl] [alpha-func]\n", argv[0]);
        return 2;
    }

    bool wantFs = false;
    int argi = 1;
    if (strcmp(argv[argi], "--fs") == 0) {
        wantFs = true;
        argi++;
    }

    gpu::ShaderUid uid;
    memset(&uid, 0, sizeof(uid));
    uid.vtxFlags = gpu::VF_NRM;  // a lit material needs normals
    uid.numTexGens = 0;
    uid.numChans = 1;
    uid.colorCtrl[0] = (argi < argc) ? parseHex(argv[argi++]) : 0;
    uid.alphaCtrl[0] = (argi < argc) ? parseHex(argv[argi++]) : uid.colorCtrl[0];
    uid.alphaFunc = (argi < argc) ? parseHex(argv[argi++]) : 0;
    uid.numTevStages = 1;

    if (uid.numChans > 0) {
        // GX_CC_RASC as the only TEV input: forces the fragment shader to read
        // the rasterized colour the vertex lighting produced.
        uid.tevOrder[0] = 0x0000000F;  // ras for both colour and alpha
    }

    std::string out = wantFs ? gpu::genVulkanFragmentShader(uid) : gpu::genVertexShader(uid);
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
