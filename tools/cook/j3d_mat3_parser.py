import struct
import os

try:
    from manifest_schema import UID_WORDS as SHADER_UID_WORDS
except ImportError:  # imported as tools.cook.j3d_mat3_parser
    from tools.cook.manifest_schema import UID_WORDS as SHADER_UID_WORDS

try:
    from j3d_shape_attrs import ShapeAttrError, material_vertex_flags
except ImportError:  # imported as tools.cook.j3d_mat3_parser
    from tools.cook.j3d_shape_attrs import ShapeAttrError, material_vertex_flags

try:
    from j3d_pipeline_key import (
        CANONICAL_BLEND_ONE, CANONICAL_BLEND_OP_ADD, CANONICAL_BLEND_ZERO, CANONICAL_CULL_BACK,
        CANONICAL_DEPTH_LEQUAL, CANONICAL_TOPO_TRIANGLE_LIST, encode_pipeline_key,
    )
except ImportError:  # imported as tools.cook.j3d_mat3_parser
    from tools.cook.j3d_pipeline_key import (
        CANONICAL_BLEND_ONE, CANONICAL_BLEND_OP_ADD, CANONICAL_BLEND_ZERO, CANONICAL_CULL_BACK,
        CANONICAL_DEPTH_LEQUAL, CANONICAL_TOPO_TRIANGLE_LIST, encode_pipeline_key,
    )

def parse_j3d_mat3(b: bytes, model_name: str, archive_name: str = ""):
    """Extracts authentic materials from a J3D (BMD/BDL) file supporting both big-endian and little-endian formats."""
    materials = []
    gaps = []
    mat3_count = 0
    if len(b) < 0x20:
        return materials, gaps, mat3_count

    magic = b[:4]
    if magic in (b'J3D2', b'J3D1'):
        is_be = True
        mat_tag = b'MAT3'
        fmt32 = '>I'
        fmt16 = '>H'
    elif magic in (b'2D3J', b'1D3J'):
        is_be = False
        mat_tag = b'3TAM'
        fmt32 = '<I'
        fmt16 = '<H'
    else:
        return materials, gaps, mat3_count

    nblocks = struct.unpack_from(fmt32, b, 0x0C)[0]
    if nblocks <= 0 or nblocks > 64:
        gaps.append(f"Invalid block count {nblocks} in {model_name}")
        return materials, gaps, mat3_count

    o = 0x20
    for block_idx in range(nblocks):
        if o + 8 > len(b):
            break
        tag = b[o:o+4]
        size = struct.unpack_from(fmt32, b, o+4)[0]
        if size <= 0 or o + size > len(b):
            break

        if tag in (b'MAT3', b'3TAM'):
            mat3_count += 1
            num_mats = struct.unpack_from(fmt16, b, o+8)[0]
            if num_mats == 0 or num_mats > 2048:
                gaps.append(f"Invalid material count {num_mats} in {model_name}")
                break

            offs = []
            for t in range(30):
                offs.append(struct.unpack_from(fmt32, b, o+0x0C+4*t)[0])

            tab0 = o + offs[0]
            block_end = o + size
            if not offs[0] or not offs[1]:
                gaps.append(f"MAT3 in {model_name} lacks its init/ID tables")
                break
            # J3DMaterialInitData records are selected through the material ID
            # table (mpMaterialID); a material sharing state with another one
            # points at the same record, so the table is usually shorter than
            # num_mats. Sequential indexing reads neighbouring tables as state.
            if o + offs[1] + 2 * num_mats > block_end:
                gaps.append(f"MAT3 ID table truncated in {model_name}")
                break
            mat_ids = [struct.unpack_from(fmt16, b, o + offs[1] + 2 * i)[0] for i in range(num_mats)]
            # Table 0 ends where the next table in the block begins; an ID past
            # that point would read another table's bytes as material state.
            init_limit = min((off for off in offs if off > offs[0]), default=size)
            if tab0 + max(mat_ids) * 0x14C + 0x14C > o + init_limit:
                gaps.append(f"MAT3 ID table points outside Table 0 in {model_name}")
                break

            # Vertex layout each material is drawn with (SHP1 vtxDesc list).
            # Without it the pipeline key cannot be reproduced, so the model is
            # skipped instead of being cooked with a fabricated layout.
            try:
                mat_vtx_flags = material_vertex_flags(b, num_mats)
            except ShapeAttrError as e:
                gaps.append(f"No vertex layout for {model_name}: {e}")
                break

            # Read material names from Table 2 (J3D String Table)
            names = []
            name_off = offs[2]
            if name_off and o + name_off + 4 <= len(b):
                ntab_o = o + name_off
                n_names = struct.unpack_from(fmt16, b, ntab_o)[0]
                for ni in range(min(num_mats, n_names)):
                    str_ent_o = ntab_o + 4 + ni * 4
                    if str_ent_o + 4 <= len(b):
                        str_off = struct.unpack_from(fmt16, b, str_ent_o + 2)[0]
                        s_start = ntab_o + str_off
                        s_end = b.find(b'\x00', s_start)
                        if s_end != -1 and s_end <= len(b):
                            names.append(b[s_start:s_end].decode('ascii', errors='ignore'))
                        else:
                            names.append(f"mat_{ni}")
                    else:
                        names.append(f"mat_{ni}")

            for m in range(num_mats):
                m_off = tab0 + mat_ids[m] * 0x14C
                mat_name = names[m] if m < len(names) else f"mat_{m}"

                # Material fields from Table 0 indices
                cull_idx = b[m_off + 0x01]
                num_chans_idx = b[m_off + 0x02]
                num_texgens_idx = b[m_off + 0x03]
                num_tevstages_idx = b[m_off + 0x04]
                zmode_idx = b[m_off + 0x06]
                alpha_comp_idx = struct.unpack_from(fmt16, b, m_off + 0x146)[0]
                blend_idx = struct.unpack_from(fmt16, b, m_off + 0x148)[0]

                # 1. Cull mode (Table 4)
                cull_val = CANONICAL_CULL_BACK
                if offs[4] and o + offs[4] + cull_idx * 4 + 4 <= len(b):
                    gx_cull = struct.unpack_from(fmt32, b, o + offs[4] + cull_idx*4)[0]
                    cull_val = min(3, gx_cull)

                # 2. Channel & texgen counts
                num_chans = 1
                if offs[6] and o + offs[6] + num_chans_idx < len(b):
                    num_chans = max(1, min(2, b[o + offs[6] + num_chans_idx]))

                num_texgens = 1
                if offs[10] and o + offs[10] + num_texgens_idx < len(b):
                    num_texgens = max(1, min(8, b[o + offs[10] + num_texgens_idx]))

                num_tevstages = 1
                if offs[19] and o + offs[19] + num_tevstages_idx < len(b):
                    num_tevstages = max(1, min(16, b[o + offs[19] + num_tevstages_idx]))

                # 3. ZMode (Table 26): [enable, func, write_enable, pad]
                depth_test = True
                depth_write = True
                depth_op = CANONICAL_DEPTH_LEQUAL
                if offs[26] and o + offs[26] + zmode_idx * 4 + 3 <= len(b):
                    zm_o = o + offs[26] + zmode_idx * 4
                    depth_test = (b[zm_o] != 0)
                    depth_op = min(7, b[zm_o + 1])
                    depth_write = (b[zm_o + 2] != 0)

                # 4. BlendMode (Table 25): [type, srcFactor, dstFactor, op]
                blend_enable = False
                src_color = CANONICAL_BLEND_ONE
                dst_color = CANONICAL_BLEND_ZERO
                src_alpha = CANONICAL_BLEND_ONE
                dst_alpha = CANONICAL_BLEND_ZERO
                color_blend_op = CANONICAL_BLEND_OP_ADD
                alpha_blend_op = CANONICAL_BLEND_OP_ADD
                if offs[25] and blend_idx != 0xFFFF and o + offs[25] + blend_idx * 4 + 4 <= len(b):
                    bl_o = o + offs[25] + blend_idx * 4
                    blend_type = b[bl_o]
                    blend_enable = (blend_type != 0)
                    src_color = min(9, b[bl_o + 1])
                    dst_color = min(9, b[bl_o + 2])
                    src_alpha = src_color
                    dst_alpha = dst_color
                    color_blend_op = min(2, b[bl_o + 3])
                    alpha_blend_op = color_blend_op

                # 5. Alpha Compare (Table 24): [comp0, ref0, op, comp1, ref1, pad...]
                alpha_func = 0
                if offs[24] and alpha_comp_idx != 0xFFFF and o + offs[24] + alpha_comp_idx * 8 + 5 <= len(b):
                    ac_o = o + offs[24] + alpha_comp_idx * 8
                    comp0 = b[ac_o] & 7
                    op = b[ac_o + 2] & 3
                    comp1 = b[ac_o + 3] & 7
                    alpha_func = comp0 | (comp1 << 3) | (op << 6)

                # 6. Build authentic full-width ShaderUid matching the native C++ engine
                uid_words = [0] * SHADER_UID_WORDS
                vtx_flags = mat_vtx_flags[m]
                uid_words[0] = vtx_flags
                uid_words[1] = num_texgens
                uid_words[2] = num_chans
                uid_words[3] = 0  # dualTex
                for c in range(min(num_chans, 2)):
                    uid_words[4 + c] = 0x0001  # colorCtrl[c]
                    uid_words[6 + c] = 0x0001  # alphaCtrl[c]
                for tg in range(min(num_texgens, 8)):
                    uid_words[8 + tg] = (5 + 3 * tg) << 7  # texMtxInfo[tg] (Tex0 row 5, Tex1 row 8...)
                uid_words[24] = num_tevstages
                uid_words[25] = 0  # numIndStages
                uid_words[74] = alpha_func  # alphaFunc BP 0xF3 bits 16-23

                # Read TEV order if available (Table 16)
                if offs[16]:
                    for s in range(min(num_tevstages, 16)):
                        ord_idx = struct.unpack_from(fmt16, b, m_off + 0xBC + s*2)[0]
                        if ord_idx != 0xFFFF and o + offs[16] + ord_idx * 4 + 4 <= len(b):
                            tord_o = o + offs[16] + ord_idx * 4
                            tc = b[tord_o] & 7
                            tm = b[tord_o + 1] & 7
                            en = 1 if (tm != 7) else 0
                            ch = b[tord_o + 2] & 7
                            order_val = (tm & 7) | ((tc & 7) << 3) | (en << 6) | ((ch & 7) << 7)
                            uid_words[26 + (s // 2)] |= (order_val << ((s % 2) * 12))

                # Read authentic TEV stage operations from Table 20 (TevStageInfo) and Table 21 (TevSwapModeInfo)
                if offs[20]:
                    for s in range(min(num_tevstages, 16)):
                        stage_idx = struct.unpack_from(fmt16, b, m_off + 0xE4 + s*2)[0]
                        if stage_idx != 0xFFFF and o + offs[20] + stage_idx * 20 + 20 <= len(b):
                            t20_o = o + offs[20] + stage_idx * 20
                            st = list(b[t20_o:t20_o+20])
                            # st[0] is dummy (0xFF); st[1:10] is color combiner; st[10:19] is alpha combiner
                            ca, cb, cc, cd, cop, cbias, cscale, cclamp, cout = st[1:10]
                            aa, ab, ac, ad, aop, abias, ascale, aclamp, aout = st[10:19]
                            tev_color = ((cd & 0xF) | ((cc & 0xF) << 4) | ((cb & 0xF) << 8) | ((ca & 0xF) << 12) |
                                         ((cbias & 3) << 16) | ((1 if cop else 0) << 18) | ((1 if cclamp else 0) << 19) |
                                         ((cscale & 3) << 20) | ((cout & 3) << 22))

                            rswap, tswap = 0, 0
                            if offs[21]:
                                swp_idx = struct.unpack_from(fmt16, b, m_off + 0x104 + s*2)[0]
                                if swp_idx != 0xFFFF and o + offs[21] + swp_idx * 4 + 2 <= len(b):
                                    swp_o = o + offs[21] + swp_idx * 4
                                    rswap = b[swp_o] & 3
                                    tswap = b[swp_o + 1] & 3

                            tev_alpha = ((rswap & 3) | ((tswap & 3) << 2) |
                                         ((ad & 7) << 4) | ((ac & 7) << 7) | ((ab & 7) << 10) | ((aa & 7) << 13) |
                                         ((abias & 3) << 16) | ((1 if aop else 0) << 18) | ((1 if aclamp else 0) << 19) |
                                         ((ascale & 3) << 20) | ((aout & 3) << 22))
                            uid_words[34 + s] = tev_color
                            uid_words[50 + s] = tev_alpha
                        else:
                            uid_words[34 + s] = 0x080A00
                            uid_words[50 + s] = 0x080A00
                else:
                    for s in range(num_tevstages):
                        uid_words[34 + s] = 0x080A00
                        uid_words[50 + s] = 0x080A00

                key = encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_op,
                                          blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                                          color_blend_op=color_blend_op, alpha_blend_op=alpha_blend_op,
                                          color_mask=15, cull_mode=cull_val, topology=CANONICAL_TOPO_TRIANGLE_LIST)
                source_desc = f"{archive_name}/{model_name}:{mat_name}" if archive_name else f"{model_name}:{mat_name}"
                materials.append((uid_words, key, source_desc))

        o += size
    return materials, gaps, mat3_count

