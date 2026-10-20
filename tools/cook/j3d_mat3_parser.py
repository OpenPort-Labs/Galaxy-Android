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
    import j3d_gx_state as gx
except ImportError:  # imported as tools.cook.j3d_mat3_parser
    import tools.cook.j3d_gx_state as gx

try:
    import j3d_material_state as jms
except ImportError:  # imported as tools.cook.j3d_mat3_parser
    import tools.cook.j3d_material_state as jms

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
                    alpha_func = gx.alpha_func_word(b[ac_o:ac_o + 4])

                # 6. Build authentic full-width ShaderUid matching the native C++ engine
                uid_words = [0] * SHADER_UID_WORDS
                vtx_flags = mat_vtx_flags[m]
                uid_words[0] = vtx_flags
                uid_words[1] = num_texgens
                uid_words[2] = num_chans
                # GXInit.c __GXInitRevisionBits sets XF_DUALTEXTRAN_F_DUALTEXTRAN_ENABLE
                # once and J3D never clears it, so the register reads 1 on every
                # draw; the per-material choice lives in texMtxInfo instead.
                uid_words[3] = 1

                # Colour/alpha channel control: J3DColorBlock loads mColorChan[0],
                # [2], [1], [3] into XF 0x100E..0x1011 (J3DMatBlock.cpp), which is
                # colour0, alpha0, colour1, alpha1 in the UID.
                chan_words = jms.channel_words(b, o + offs[7] if offs[7] else 0, block_end, m_off, fmt16)
                if chan_words is None:
                    gaps.append(f"{model_name}:{mat_name} colour channel index outside its table")
                else:
                    if num_chans >= 1:
                        uid_words[4] = chan_words[0]
                        uid_words[6] = chan_words[1]
                    if num_chans >= 2:
                        uid_words[5] = chan_words[2]
                        uid_words[7] = chan_words[3]
                tex_coords = jms.tex_coord_words(b, o + offs[11] if offs[11] else 0, block_end, m_off, num_texgens, fmt16)
                if tex_coords is None:
                    gaps.append(f"{model_name}:{mat_name} texcoord index outside its table")
                else:
                    for tg in range(min(num_texgens, 8)):
                        uid_words[8 + tg] = tex_coords[tg]
                uid_words[24] = num_tevstages
                uid_words[25] = 0  # numIndStages
                uid_words[74] = alpha_func  # alphaFunc BP 0xF3 bits 16-23

                # TEV order, packed two stages per BP 0x28+n register.  A
                # one-stage block still writes both halves: J3DTevBlock1::load
                # passes the NULL texcoord/map/channel for stage 1.
                orders = jms.tev_order_fields(b, o + offs[16] if offs[16] else 0, block_end, m_off, num_tevstages, fmt16)
                if orders is None:
                    gaps.append(f"{model_name}:{mat_name} tev order index outside its table")
                else:
                    if num_tevstages == 1:
                        orders.append(gx.TEV_ORDER_NULL)
                    for s, field in enumerate(orders[:16]):
                        uid_words[26 + (s // 2)] |= (field << ((s % 2) * 12))

                stages = jms.tev_stage_words(b, o + offs[20] if offs[20] else 0,
                                             o + offs[21] if offs[21] else 0,
                                             block_end, m_off, num_tevstages, fmt16)
                if stages is None:
                    gaps.append(f"{model_name}:{mat_name} tev stage index outside its table")
                else:
                    for s, (tev_color, tev_alpha) in enumerate(zip(*stages)):
                        uid_words[34 + s] = tev_color
                        uid_words[50 + s] = tev_alpha

                # TEV constant/swap selectors, BP 0xF6..0xFD.  J3DTevBlock writes
                # all eight from four swap-mode tables and the material's
                # K-colour/K-alpha selectors.  J3DTevBlock1 (a single TEV stage)
                # never writes them, so those registers still hold the previous
                # draw's values: the words stay 0 and the entry prewarms the
                # pipeline without claiming to predict that draw's UID.
                if num_tevstages >= 2:
                    ksel = jms.tev_ksel_words(b, o + offs[22] if offs[22] else 0, block_end, m_off,
                                               num_tevstages, fmt16)
                    if ksel is None:
                        gaps.append(f"{model_name}:{mat_name} has no TEV swap-mode table")
                    else:
                        for pair in range(8):
                            uid_words[66 + pair] = ksel[pair]

                key = encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_op,
                                          blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                                          color_blend_op=color_blend_op, alpha_blend_op=alpha_blend_op,
                                          color_mask=15, cull_mode=cull_val, topology=CANONICAL_TOPO_TRIANGLE_LIST)
                source_desc = f"{archive_name}/{model_name}:{mat_name}" if archive_name else f"{model_name}:{mat_name}"
                materials.append((uid_words, key, source_desc))

        o += size
    return materials, gaps, mat3_count

