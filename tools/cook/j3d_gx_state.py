"""J3D material data -> GX register words, as the game itself writes them.

The runtime ShaderUid is built from the BP/XF register shadows, so a manifest
entry only predicts it if the cooker reproduces the exact words J3D emits.  Each
function here is a transliteration of the decompiled writer it names; the
references live in decomp/ and are cited per function.

Nothing in this module guesses: a value the material data does not determine is
rejected by the caller rather than filled with a plausible-looking constant.
"""

# GXColorSrc
GX_SRC_REG = 0
GX_SRC_VTX = 1
# GXDiffuseFn
GX_DF_NONE = 0
GX_DF_SIGN = 1
GX_DF_CLAMP = 2
# GXAttnFn
GX_AF_SPEC = 0
GX_AF_NONE = 1
GX_AF_ADD = 2
GX_AF_SUB = 3

# J3DGDTevs.cpp: J3DColorChanInfo {mEnable, mMatSrc, mLightMask, mDiffuseFn,
# mAttnFn, mAmbSrc, pad[2]}.  ambSrc 0xFF means "use the register".
DEFAULT_COLOR_CHAN = bytes((0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0xFF, 0xFF))

# J3DGD.cpp: J3DGDSetTevOrder maps GXChannelID to the 3-bit BP field.
TEV_ORDER_CHANNEL = (0, 1, 0, 1, 0, 1, 7, 5, 6, 0, 0, 0, 0, 0, 0, 7)

GX_TEXMAP_NULL = 0xFF
GX_TEX_DISABLE = 0x100


def set_chan_ctrl(enable, mat_src, amb_src, light_mask, diffuse_fn, attn_fn):
    """XF_COLOR0CNTRL word.  J3DTevs.hpp: setChanCtrlMacro, the exact expression
    J3DColorChan::load writes to XF 0x100E..0x1011."""
    return (
        (mat_src & 1)
        | ((enable & 1) << 1)
        | ((light_mask & 0x0F) << 2)
        | ((amb_src & 1) << 6)
        | ((GX_DF_NONE if attn_fn == GX_AF_SPEC else diffuse_fn & 3) << 7)
        | ((1 if attn_fn != GX_AF_NONE else 0) << 9)
        | ((1 if attn_fn != GX_AF_SPEC else 0) << 10)
        | (((light_mask >> 4) & 0x0F) << 11)
    )


# J3DTevs.hpp: J3DColorChan::getAttnFn indexes this with the two attn bits of
# the stored ID, so the value that reaches XF is not the material's attnFn.
ATTN_REMAP = (GX_AF_ADD, GX_AF_SPEC, GX_AF_ADD, GX_AF_NONE)


def color_chan_id(record):
    """One 8-byte J3DColorChanInfo record -> the 16-bit XF channel-control word.

    J3DTevs.hpp: J3DColorChan(J3DColorChanInfo const&) resolves ambSrc 0xFF to
    the register source before packing; J3DGXColorSrc values are already GX.
    """
    if len(record) < 6:
        raise ValueError("color channel record truncated")
    amb_src = record[5]
    if amb_src == 0xFF:
        amb_src = GX_SRC_REG
    return set_chan_ctrl(record[0], record[1], amb_src, record[2], record[3], record[4])


def color_chan_word(record):
    """The word XF 0x100E..0x1011 actually receives for one J3DColorChan.

    J3DColorChan keeps only calcColorChanID(info) and J3DColorChan::load
    rebuilds the register from that ID, so attnFn survives as two ID bits and
    is re-derived through ATTN_REMAP rather than reused.  A material whose
    attnFn is GX_AF_NONE therefore still lands on bit 9 set, which is why the
    cooked words never matched the runtime ones.
    """
    cid = color_chan_id(record)
    return set_chan_ctrl(
        (cid >> 1) & 1,
        cid & 1,
        (cid >> 6) & 1,
        ((cid >> 2) & 0x0F) | (((cid >> 11) & 0x0F) << 4),
        (cid >> 7) & 3,
        ATTN_REMAP[(cid >> 9) & 3],
    )


def tev_op_byte(p1, p2, p3, p4, p5):
    """J3DTevs.hpp: J3DTevStage::setTevColorOp / setTevAlphaOp share this pack.

    The five source bytes are (bias, b, c, clamp, regId) and the shared nibble
    folds the bias/compare/scale fields together when bias is 2 or 3.
    """
    byte = (p1 & 1) << 2
    if p1 <= 1:
        byte |= ((p3 & 3) << 4) | (p2 & 3)
    else:
        byte |= (((p1 >> 1) & 3) << 4) | 3
    return byte | ((p4 & 1) << 3) | ((p5 & 3) << 6)


def tev_color_word(info):
    """J3DTevStage bytes 1..4 as BP 0xC0+2n.  J3DTevs.hpp setTevStageInfo reads
    color A/B from bytes 1/2 and C/D from bytes 3/4, op from bytes 5..9."""
    return (tev_op_byte(info[5], info[6], info[7], info[8], info[9]) << 16
            | ((info[1] & 0xF) << 12) | ((info[2] & 0xF) << 8)
            | ((info[3] & 0xF) << 4) | (info[4] & 0xF))


def tev_alpha_word(info, ras_sel=0, tex_sel=0):
    """J3DTevStage bytes 0xA..0x12 as BP 0xC1+2n.

    setAlphaABCD splits alpha C across mTevAlphaAB bit 0 and mTevSwapModeInfo
    bit 7, and alpha D lives in mTevSwapModeInfo bits 4..6.
    """
    a, b, c, d = info[0x0A] & 7, info[0x0B] & 7, info[0x0C] & 7, info[0x0D] & 7
    ab = (a << 5) | (b << 2) | (c >> 1)
    swap = (ras_sel & 3) | ((tex_sel & 3) << 2) | (d << 4) | ((c & 1) << 7)
    return (tev_op_byte(info[0x0E], info[0x0F], info[0x10], info[0x11], info[0x12]) << 16
            | ab << 8 | swap)


def tev_order_field(record):
    """One 4-byte J3DTevOrderInfo -> the 12-bit BP TEV order field.

    J3DGD.cpp: J3DGDSetTevOrder builds the field as
    map | coord << 3 | enable << 6 | c2r[color] << 7 with
    enable = map != GX_TEXMAP_NULL && !(map & GX_TEX_DISABLE).  The cooked
    manifest previously treated map == 7 as "disabled" and skipped the c2r
    mapping, which is why every runtime order word differed.
    """
    if len(record) < 3:
        raise ValueError("tev order record truncated")
    tex_coord, tex_map, color_chan = record[0], record[1], record[2]
    # J3DGDSetTevOrder clamps an out-of-range coord to GX_TEXCOORD0; reading the
    # raw 0xFF as 7 is what put 0x3BF in the high half of the cooked words.
    if tex_coord >= 8:
        tex_coord = 0
    enable = 1 if (tex_map != GX_TEXMAP_NULL and not (tex_map & GX_TEX_DISABLE)) else 0
    return (
        (tex_map & 7)
        | ((tex_coord & 7) << 3)
        | (enable << 6)
        | ((TEV_ORDER_CHANNEL[color_chan & 0x0F]) << 7)
    )


# J3DTevBlock1::load passes (GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL)
# for the second stage of its shared BP 0x28 word.
TEV_ORDER_NULL = (GX_TEXMAP_NULL & 7) | (TEV_ORDER_CHANNEL[0x0F] << 7)


def tev_order_word(stage_fields):
    """Packs two consecutive stages into one BP 0x28+n word (bits 0 and 12)."""
    word = 0
    for i, field in enumerate(stage_fields[:2]):
        word |= (field & 0xFFF) << (12 * i)
    return word & 0xFFFFFF


def tev_ksel_word(color_sel1, alpha_sel1, color_sel2, alpha_sel2, chan1, chan2):
    """BP 0xF6..0xFD word.  J3DGD.hpp: J3DGDSetTevKonstantSel_SwapModeTable."""
    return (
        (chan1 & 3)
        | ((chan2 & 3) << 2)
        | ((color_sel1 & 0x1F) << 4)
        | ((alpha_sel1 & 0x1F) << 9)
        | ((color_sel2 & 0x1F) << 14)
        | ((alpha_sel2 & 0x1F) << 19)
    ) & 0xFFFFFF


# GXEnum.h: GXTexGenSrc / GXTexGenType.  J3DGD.cpp: J3DGDSetTexCoordGen maps a
# source to a matrix row (and a form), and a type to proj/tgType.
GX_TG_ROW_FORM = {
    0: (0, 1), 1: (1, 1), 2: (3, 1), 3: (4, 1),               # POS NRM BINRM TANGENT
    4: (5, 0), 5: (6, 0), 6: (7, 0), 7: (8, 0),               # TEX0..TEX3
    8: (9, 0), 9: (10, 0), 10: (11, 0), 11: (12, 0),          # TEX4..TEX7
    20: (2, 0), 21: (2, 0),                                   # COLOR0 COLOR1
}
GX_TG_MTX3X4 = 0
GX_TG_BUMP0 = 2
GX_TG_SRTG = 10


def tex_coord_gen_word(gen_type, gen_src):
    """One J3DTexCoordInfo -> the XF 0x1040+n texgen word the UID keeps.

    J3DGD.hpp: XF_REG_TEX = proj | form << 2 | tgType << 4 | row << 7 |
    embossRow << 12 | embossLit << 15, with the defaults J3DGDSetTexCoordGen
    starts from (row 5, emboss row 5).
    """
    row, form = GX_TG_ROW_FORM.get(gen_src, (5, 0))
    emboss_row, emboss_lit, tg_type = 5, 0, 0
    if 12 <= gen_src <= 19:  # GX_TG_TEXCOORD0..7 only set the emboss row
        emboss_row = gen_src - 12
    if GX_TG_BUMP0 <= gen_type <= GX_TG_BUMP0 + 7:
        tg_type, emboss_lit = 1, gen_type - GX_TG_BUMP0
    elif gen_type == GX_TG_SRTG:
        tg_type = 2 if gen_src == 20 else 3
    proj = 1 if gen_type == GX_TG_MTX3X4 else 0
    return (proj << 1) | (form << 2) | (tg_type << 4) | (row << 7) | (emboss_row << 12) | (emboss_lit << 15)


def alpha_func_word(record):
    """J3DAlphaCompInfo -> the BP 0xF3 comparison byte the UID keeps.

    J3DGD.hpp: J3DGDSetAlphaCompare writes
    cmp0 << 16 | cmp1 << 19 | op << 22, and makeAlphaCmpTable() is the identity
    for (comp0, op, comp1), so the material bytes map straight through.
    """
    if len(record) < 4:
        raise ValueError("alpha compare record truncated")
    return (record[0] & 7) | ((record[3] & 7) << 3) | ((record[2] & 3) << 6)
