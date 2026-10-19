#include "gl_renderer_internal.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace gpu {

GLuint Renderer::Impl::vao(u32 flags) {
    auto it = vaos.find(flags);
    if (it != vaos.end()) return it->second;
    GLuint v;
    glGenVertexArrays(1, &v);
    glBindVertexArray(v);
    u32 off = 0;
    auto attr = [&](GLuint loc, GLint size, GLenum type, bool normalized, bool integer, u32 bytes) {
        glEnableVertexAttribArray(loc);
        if (integer) {
            glVertexAttribIFormat(loc, size, type, off);
        } else {
            glVertexAttribFormat(loc, size, type, normalized, off);
        }
        glVertexAttribBinding(loc, 0);
        off += bytes;
    };
    attr(0, 3, GL_FLOAT, false, false, 12);
    if (flags & VF_POSMTX) attr(1, 1, GL_UNSIGNED_INT, false, true, 4);
    if (flags & VF_NRM) attr(2, 3, GL_FLOAT, false, false, 12);
    if (flags & VF_NBT) {
        attr(3, 3, GL_FLOAT, false, false, 12);
        attr(4, 3, GL_FLOAT, false, false, 12);
    }
    if (flags & VF_CLR0) attr(5, 4, GL_UNSIGNED_BYTE, true, false, 4);
    if (flags & VF_CLR1) attr(6, 4, GL_UNSIGNED_BYTE, true, false, 4);
    for (int i = 0; i < 8; i++) {
        if (flags & (VF_TEX0 << i)) attr(7 + i, 2, GL_FLOAT, false, false, 8);
    }
    if (flags & VF_TEXMTX) attr(15, 2, GL_UNSIGNED_INT, false, true, 8);
    glBindVertexArray(0);
    vaos[flags] = v;
    return v;
}

GLuint Renderer::Impl::sampler(u32 mode0, u32 mode1, u32 levels) {
    u64 key = ((u64)(mode0 & 0xFFFFFF) << 32) | ((mode1 & 0xFFFF) << 8) | (levels > 1 ? 1 : 0);
    auto it = samplers.find(key);
    if (it != samplers.end()) return it->second;
    GLuint s;
    glGenSamplers(1, &s);
    static const GLenum wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, ((mode0 >> 4) & 1) ? GL_LINEAR : GL_NEAREST);
    u32 minf = (mode0 >> 5) & 7;
    GLenum min;
    bool mips = levels > 1;
    switch (minf) {
    case 0: min = GL_NEAREST; break;
    case 1: min = mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST; break;
    case 2: min = mips ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST; break;
    case 5: min = mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR; break;
    case 6: min = mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR; break;
    default: min = GL_LINEAR; break;
    }
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
    if (!mips) {
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, 0.0f);
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, 0.0f);
    } else {
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, std::min((float)(levels - 1), (mode1 & 0xFF) / 16.0f));
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, std::min((float)(levels - 1), ((mode1 >> 8) & 0xFF) / 16.0f));
    }
    samplers[key] = s;
    return s;
}

void Renderer::Impl::upload(Prepared& p, bool syncPrograms) {
    const Frame& f = *p.frame;
    FrameBuffers& fb = frameBufs[frameBufNext];
    frameBufNext = (frameBufNext + 1) % 3;
    glBindVertexArray(0);
    auto put = [&fb](int i, GLenum target, const void* data, size_t bytes) {
        glBindBuffer(target, fb.buf[i]);
        if (bytes > fb.cap[i]) {
            fb.cap[i] = bytes + bytes / 4 + 4096;
        }
        glBufferData(target, (GLsizeiptr)fb.cap[i], nullptr, GL_DYNAMIC_DRAW);
        if (bytes) {
            glBufferSubData(target, 0, (GLsizeiptr)bytes, data);
        }
    };
    put(0, GL_ARRAY_BUFFER, f.verts.data(), f.verts.size() * 4);
    put(1, GL_SHADER_STORAGE_BUFFER, p.vtxRec.data(), p.vtxRec.size() * 4);
    put(2, GL_ELEMENT_ARRAY_BUFFER, p.indices.data(), p.indices.size() * 4);
    put(3, GL_SHADER_STORAGE_BUFFER, p.rows.data(), p.rows.size() * 4);
    put(4, GL_SHADER_STORAGE_BUFFER, p.recs.data(), p.recs.size() * 4);
    put(5, GL_SHADER_STORAGE_BUFFER, p.states.data(), p.states.size() * 4);

    u32 rowCount = (u32)(p.rows.size() / 4);
    u32 rowH = (rowCount + 2047) / 2048;
    if (rowH < 1) rowH = 1;
    p.rows.resize((size_t)rowH * 2048 * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, fb.tex[0]);
    if (rowH > fb.rowCapH) {
        fb.rowCapH = rowH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2048, (GLsizei)fb.rowCapH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)rowH, GL_RGBA, GL_FLOAT, p.rows.data());

    u32 recCount = (u32)p.recs.size();
    u32 recH = (recCount + 2047) / 2048;
    if (recH < 1) recH = 1;
    p.recs.resize((size_t)recH * 2048, 0u);
    glBindTexture(GL_TEXTURE_2D, fb.tex[1]);
    if (recH > fb.recCapH) {
        fb.recCapH = recH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 2048, (GLsizei)fb.recCapH, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)recH, GL_RED_INTEGER, GL_UNSIGNED_INT, p.recs.data());

    u32 stCount = (u32)(p.states.size() / 4);
    u32 stH = (stCount + 2047) / 2048;
    if (stH < 1) stH = 1;
    p.states.resize((size_t)stH * 2048 * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, fb.tex[2]);
    if (stH > fb.stateCapH) {
        fb.stateCapH = stH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2048, (GLsizei)fb.stateCapH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)stH, GL_RGBA, GL_FLOAT, p.states.data());

    u32 vtxRecCount = (u32)p.vtxRec.size();
    u32 vtxRecH = (vtxRecCount + 2047) / 2048;
    if (vtxRecH < 1) vtxRecH = 1;
    p.vtxRec.resize((size_t)vtxRecH * 2048, 0u);
    glBindTexture(GL_TEXTURE_2D, fb.tex[3]);
    if (vtxRecH > fb.vtxRecCapH) {
        fb.vtxRecCapH = vtxRecH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 2048, (GLsizei)fb.vtxRecCapH, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)vtxRecH, GL_RED_INTEGER, GL_UNSIGNED_INT, p.vtxRec.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    vbo = fb.buf[0];
    recVbo = fb.buf[1];
    ibo = fb.buf[2];
    rowBuf = fb.buf[3];
    recBuf = fb.buf[4];
    stateBuf = fb.buf[5];
    rowTex = fb.tex[0];
    recTex = fb.tex[1];
    stateTex = fb.tex[2];
    vtxRecTex = fb.tex[3];

    for (auto& img : f.textures) {
        if (img->glTex && img->glVersion == img->version) continue;
        if (!img->glTex) glGenTextures(1, &img->glTex);
        glBindTexture(GL_TEXTURE_2D, img->glTex);
        const u32* px = img->rgba.data();
        u32 w = img->width, h = img->height;
        for (u32 l = 0; l < img->levels; l++) {
            glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            px += (size_t)w * h;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)img->levels - 1);
        img->glVersion = img->version;
        static const bool keepPixels = debugEnv("PETARI_DUMP") != nullptr;
        if (!keepPixels) {
            std::vector<uint32_t>().swap(img->rgba);
        }
    }

    std::vector<GLuint> progs(p.uids.size());
    for (size_t i = 0; i < p.uids.size(); i++) {
        progs[i] = program(p.uids[i], syncPrograms);
    }
    for (Item& it : p.items) {
        if (it.type == IT_DRAW) {
            it.program = progs[it.uid];
        }
    }
    p.cutProgs.assign(p.uids.size(), 0);
}

void Renderer::Impl::workerMain() {
    for (;;) {
        std::shared_ptr<const Frame> frame;
        std::unique_ptr<Prepared> out;
        {
            std::unique_lock<std::mutex> lock(workLock);
            workCv.wait(lock, [&] { return wanted != nullptr; });
            frame = std::move(wanted);
            wanted.reset();
            out = spare ? std::move(spare) : std::unique_ptr<Prepared>(new Prepared());
        }
        int64_t t0 = port_host_time_ns();
        workerBuilder.build(std::move(frame), *out);
        port_perf_prepare(port_host_time_ns() - t0);
        static const bool perfLog = debugEnv("PETARI_PERFLOG") != nullptr;
        if (perfLog && out->number % 600 == 0) {
            size_t batches = 0, copies = 0;
            for (const Item& it : out->items) {
                batches += it.type == IT_DRAW;
                copies += it.type == IT_COPY;
            }
            port_log("perf: prepared frame %llu: %zu batches, %zu EFB copies, %zu shader keys, %zu indices",
                     (unsigned long long)out->number, batches, copies, out->uids.size(), out->indices.size());
        }
        std::lock_guard<std::mutex> lock(workLock);
        if (built) {
            spare = std::move(built);
        }
        built = std::move(out);
    }
}

void Renderer::Impl::requestLatest() {
    std::lock_guard<std::mutex> lock(workLock);
    auto latest = frameQueue().latest();
    if (latest && latest->number != lastRequested) {
        lastRequested = latest->number;
        wanted = std::move(latest);
        workCv.notify_one();
    }
}

void onFramePublished() {
    if (sImpl) {
        sImpl->requestLatest();
    }
}

}  // namespace gpu
