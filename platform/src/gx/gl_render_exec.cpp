#include "gl_renderer_internal.h"
#include "port/port.h"

namespace gpu {

GLint Renderer::Impl::firstOrdinalLoc(GLuint prog) {
    auto it = ordinalLocs.find(prog);
    if (it != ordinalLocs.end()) return it->second;
    GLint loc = glGetUniformLocation(prog, "u_firstOrdinal");
    ordinalLocs[prog] = loc;
    return loc;
}

void Renderer::Impl::drawItem(const Item& it, GlStateTracker& gs, bool toHudTarget, bool tagPlayer,
                              bool cutActive, const std::vector<GLuint>& cutProgs,
                              const std::vector<std::shared_ptr<TexImage>>& textures,
                              EfbTarget& t, const EyeView* eye, bool vrEye, GLuint& lastProgram,
                              GLuint& lastVao, GLint& ordinalLoc) {
    GLuint prog = it.program;
    if (!prog && it.uid < this->cur->uids.size()) {
        prog = program(this->cur->uids[it.uid], true);
    }
    if (!prog || !it.idxCount) return;

    u32 nativeW = cur->nativeW, nativeH = cur->nativeH;
    float fx = (float)t.width / (float)nativeW, fy = (float)t.height / (float)nativeH;

    if (it.useVr && vrEye) {
        gs.setViewport(0, 0, t.width, t.height);
        gs.setScissor(false, 0, 0, 0, 0);
    } else {
        int vx, vw, sx, sr;
        float targetAspect = (float)t.width / (float)t.height;
        const float k169 = 16.0f / 9.0f;
        if (it.hudSafe && targetAspect > k169 + 0.01f) {
            float hudW = (float)t.height * k169;
            float hudOfsX = ((float)t.width - hudW) * 0.5f;
            float hudFx = hudW / (float)nativeW;
            vx = (int)lroundf(hudOfsX + it.vp[0] * hudFx);
            vw = (int)lroundf(it.vp[2] * hudFx);
            sx = (int)lroundf(hudOfsX + it.sc[0] * hudFx);
            sr = (int)lroundf(hudOfsX + it.sc[2] * hudFx);
        } else {
            vx = (int)lroundf(it.vp[0] * fx);
            vw = (int)lroundf(it.vp[2] * fx);
            sx = (int)lroundf(it.sc[0] * fx);
            sr = (int)lroundf(it.sc[2] * fx);
        }
        int vh = (int)lroundf(it.vp[3] * fy);
        int vy = t.height - (int)lroundf(it.vp[1] * fy) - vh;
        gs.setViewport(vx, vy, vw, vh);
        int st = (int)lroundf(it.sc[1] * fy), sb = (int)lroundf(it.sc[3] * fy);
        if (it.hud && eye && eye->flatStereo && eye->stereo[3] != 0.0f) {
            float shift = eye->stereo[3];
            auto moved = [&](int x) {
                float n = (x * 2.0f / t.width - 1.0f) * (1.0f + fabsf(shift)) + shift;
                return (int)lroundf((n + 1.0f) * 0.5f * t.width);
            };
            sx = moved(sx);
            sr = moved(sr);
        }
        gs.setScissor(true, sx, t.height - sb, sr - sx > 0 ? sr - sx : 0, sb - st > 0 ? sb - st : 0);
    }

    if (toHudTarget) {
        hudDraws++;
        gs.setColorMask((it.colorMask & 1) ? 15 : 0);
        gs.setBlend(true, it.blend ? it.srcRGB : GL_ONE, it.blend ? it.dstRGB : GL_ZERO,
                    it.blend ? GL_ONE : GL_CONSTANT_ALPHA, it.blend ? GL_ONE_MINUS_SRC_ALPHA : GL_ZERO,
                    it.blend ? it.eq : GL_FUNC_ADD, GL_FUNC_ADD, 1.0f);
    } else {
        gs.setColorMask(it.colorMask);
        if (it.blend || it.dstAlpha) {
            GLenum srcA = it.srcRGB, dstA = it.dstRGB;
            if (it.dstAlpha) {
                srcA = GL_CONSTANT_ALPHA;
                dstA = GL_ZERO;
            }
            gs.setBlend(true, it.blend ? it.srcRGB : GL_ONE, it.blend ? it.dstRGB : GL_ZERO, srcA, dstA,
                        it.blend ? it.eq : GL_FUNC_ADD, it.dstAlpha ? GL_FUNC_ADD : it.eq, it.dstAlphaValue);
        } else {
            gs.setBlend(false, 0, 0, 0, 0, 0, 0, 0.0f);
        }
    }
    gs.setDepth(it.depthTest, it.depthFunc, it.depthTest && it.depthWrite);
    gs.setCull(it.cull, it.cullFace);
    if (tagPlayer) {
        gs.setStencil(!toHudTarget && it.depthTest && it.depthWrite, it.player ? 1 : 0);
    }

    if (cutActive && it.useVr && it.uid < cutProgs.size() && cutProgs[it.uid]) {
        prog = cutProgs[it.uid];
    }
    if (prog != lastProgram) {
        glUseProgram(prog);
        lastProgram = prog;
        ordinalLoc = firstOrdinalLoc(prog);
    }
    glUniform1ui(ordinalLoc, it.firstOrdinal);
    for (int u = 0; u < 8; u++) {
        u32 tr = it.tex[u];
        if (!(it.texMask & (1u << u)) || tr == ~0u) continue;
        if (tr & 0x80000000u) {
            u32 addr = (tr & 0x7FFFFFFF) << 5;
            auto cit = copies.find(copyKey(copySlot, addr));
            if (cit == copies.end() && copySlot != 0) {
                cit = copies.find(copyKey(0, addr));
            }
            GLuint ctex = (cit != copies.end()) ? cit->second.tex : 0;
            gs.bindTexture(u, ctex, sampler(it.texMode0[u], it.texMode1[u], 1));
        } else if (tr < textures.size()) {
            gs.bindTexture(u, textures[tr]->glTex, sampler(it.texMode0[u], it.texMode1[u], textures[tr]->levels));
        }
    }

    GLuint v = vao(it.flags);
    if (v != lastVao) {
        glBindVertexArray(v);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        lastVao = v;
    }
    glBindVertexBuffer(0, vbo, (GLintptr)it.firstWord * 4, (GLsizei)(vtxWords(it.flags) * 4));
    glDrawElements(it.prim, (GLsizei)it.idxCount, GL_UNSIGNED_INT, (const void*)((uintptr_t)it.idxOffset * 4));
}

void Renderer::Impl::execute(EfbTarget& efb, const EyeView* eye, EfbTarget* hud, HudMode hudMode, Renderer& r) {
    const std::vector<Item>& items = cur->items;
    const std::vector<std::shared_ptr<TexImage>>& textures = cur->frame->textures;
    u32 clearAR = cur->clearAR, clearGB = cur->clearGB, clearZ = cur->clearZ;
    if (hudMode == HudMode::Target && !hud) {
        hudMode = HudMode::Inline;
    }
    EyeBlock eb;
    memset(&eb, 0, sizeof(eb));
    bool vrEye = eye && !eye->flatStereo;
    if (vrEye) {
        for (int i = 0; i < 16; i++) {
            eb.vrView[i / 4][i % 4] = eye->view[i];
            eb.vrProj[i / 4][i % 4] = eye->proj[i];
        }
        eb.vrFlags[0] = 1;
        memcpy(eb.vrFocus, eye->focus, sizeof(eb.vrFocus));
        memcpy(eb.vrCut, eye->cut, sizeof(eb.vrCut));
        memcpy(eb.vrEyePos, eye->eyePos, sizeof(eb.vrEyePos));
    } else if (eye) {
        eb.vrFlags[0] = 2;
        memcpy(eb.vrStereo, eye->stereo, sizeof(eb.vrStereo));
        eb.vrStereo2[0] = eye->pointer[0];
        eb.vrStereo2[1] = eye->pointer[1];
    }
    eyeUbo = eyeUbos[eyeUboNext];
    eyeUboNext = (eyeUboNext + 1) % 8;
    glBindBuffer(GL_UNIFORM_BUFFER, eyeUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(eb), &eb);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, eyeUbo);

    bool cutActive = vrEye && eye->focus[3] > 0.0f;
    std::vector<GLuint>& cutProgs = this->cur->cutProgs;
    copySlot = eye ? 1 + (eye->index & 1) : 0;
    static const bool sDumpEye = debugEnv("PETARI_COPYDUMP_EYE") != nullptr;
    dumpingCopies = (sDumpEye ? (eye && eye->index == 0) : !eye) && debugEnv("PETARI_COPYDUMP") && copyDumpFrame != this->cur->number;
    if (dumpingCopies) {
        copyDumpFrame = this->cur->number;
    }
    if (cutActive) {
        const std::vector<ShaderUid>& frameUids = this->cur->uids;
        for (size_t i = 0; i < frameUids.size() && i < cutProgs.size(); i++) {
            if (!cutProgs[i] && !frameUids[i].hud) {
                ShaderUid u = frameUids[i];
                u.cutaway = 1;
                cutProgs[i] = program(u, false);
            }
        }
    }

    auto clearTarget = [&](EfbTarget& t, bool transparent) {
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        if (transparent) {
            glClearColor(0, 0, 0, 0);
        } else {
            glClearColor((clearAR & 255) / 255.0f, ((clearGB >> 8) & 255) / 255.0f, (clearGB & 255) / 255.0f, ((clearAR >> 8) & 255) / 255.0f);
        }
        glClearDepthf(clearZ / 16777215.0f);
        glStencilMask(0xFF);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    };
    clearTarget(efb, false);
    if (hudMode == HudMode::Target) {
        clearTarget(*hud, true);
    }
    bool inHudSection = false;
    bool tagPlayer = eye && eye->depthSnapshot;
    snapshotTaken = false;
    hudDraws = 0;
    if (tagPlayer) {
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    EfbTarget* curTarget = &efb;
    glBindFramebuffer(GL_FRAMEBUFFER, curTarget->fbo);
    GLuint lastProgram = ~0u, lastVao = ~0u;
    GLint ordinalLoc = -1;
    GlStateTracker gs;
    gs.forget();

    glBindBufferBase(GL_UNIFORM_BUFFER, 2, eyeUbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rowBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, recBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, stateBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, recVbo);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, rowTex);
    glBindSampler(8, 0);

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, recTex);
    glBindSampler(9, 0);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, stateTex);
    glBindSampler(10, 0);

    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, vtxRecTex);
    glBindSampler(11, 0);

    glActiveTexture(GL_TEXTURE0);

    int stopAfter = gDebugStopAfterDraws;
    for (const Item& it : items) {
        if (stopAfter >= 0 && it.type == IT_DRAW) {
            if (stopAfter < (int)it.draws) break;
            stopAfter -= (int)it.draws;
        }
        if (it.type == IT_MARKER) {
            if (it.marker == MARK_SCENE_DEPTH && tagPlayer && curTarget == &efb) {
                EfbTarget& snap = *eye->depthSnapshot;
                glBindFramebuffer(GL_READ_FRAMEBUFFER, efb.fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, snap.fbo);
                glDisable(GL_SCISSOR_TEST);
                glBlitFramebuffer(0, 0, efb.width, efb.height, 0, 0, snap.width, snap.height,
                                  GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, curTarget->fbo);
                gs.forget();
                snapshotTaken = true;
                continue;
            }
            if (it.marker != MARK_HUD_BEGIN && it.marker != MARK_HUD_END) continue;
            inHudSection = it.marker == MARK_HUD_BEGIN;
            if (hudMode == HudMode::Target) {
                curTarget = inHudSection ? hud : &efb;
                glBindFramebuffer(GL_FRAMEBUFFER, curTarget->fbo);
            }
            continue;
        }
        if (inHudSection && hudMode == HudMode::Skip) continue;
        bool toHudTarget = inHudSection && hudMode == HudMode::Target;
        if (it.type == IT_COPY) {
            static const bool noCopies = debugEnv("PETARI_NOCOPY") != nullptr;
            if (noCopies && !((it.copy[9] >> 14) & 1)) continue;
            doCopy(it, *curTarget, r, eye != nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, curTarget->fbo);
            lastProgram = ~0u;
            lastVao = ~0u;
            gs.forget();
            continue;
        }
        drawItem(it, gs, toHudTarget, tagPlayer, cutActive, cutProgs, textures, *curTarget, eye, vrEye,
                 lastProgram, lastVao, ordinalLoc);
    }
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    if (tagPlayer) glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);

    const GLenum depthOnly[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    glBindFramebuffer(GL_FRAMEBUFFER, efb.fbo);
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, depthOnly);
    if (hudMode == HudMode::Target) {
        glBindFramebuffer(GL_FRAMEBUFFER, hud->fbo);
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, depthOnly);
    }
}

}  // namespace gpu
