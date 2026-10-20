"""Canonical PipelineKey field encoding (mirrors platform/src/gx/vk/vk_pipeline_key.h).

Split out of j3d_mat3_parser.py so each maintained file stays within the
repository's 300-line limit.
"""

# Canonical Enums matching platform/src/gx/vk/vk_types.h
CANONICAL_DEPTH_NEVER = 0
CANONICAL_DEPTH_LESS = 1
CANONICAL_DEPTH_EQUAL = 2
CANONICAL_DEPTH_LEQUAL = 3
CANONICAL_DEPTH_GREATER = 4
CANONICAL_DEPTH_NEQUAL = 5
CANONICAL_DEPTH_GEQUAL = 6
CANONICAL_DEPTH_ALWAYS = 7

CANONICAL_BLEND_ZERO = 0
CANONICAL_BLEND_ONE = 1
CANONICAL_BLEND_SRCCLR = 2
CANONICAL_BLEND_INVSRCCLR = 3
CANONICAL_BLEND_SRCATTR = 4
CANONICAL_BLEND_INVSRCATTR = 5
CANONICAL_BLEND_DSTATTR = 6
CANONICAL_BLEND_INVDSTATTR = 7
CANONICAL_BLEND_DSTCLR = 8
CANONICAL_BLEND_INVDSTCLR = 9

CANONICAL_BLEND_OP_ADD = 0
CANONICAL_BLEND_OP_SUBTRACT = 1
CANONICAL_BLEND_OP_REVERSE_SUBTRACT = 2

CANONICAL_CULL_NONE = 0
CANONICAL_CULL_FRONT = 1
CANONICAL_CULL_BACK = 2
CANONICAL_CULL_FRONT_AND_BACK = 3

CANONICAL_TOPO_POINT_LIST = 0
CANONICAL_TOPO_LINE_LIST = 1
CANONICAL_TOPO_LINE_STRIP = 2
CANONICAL_TOPO_TRIANGLE_LIST = 3
CANONICAL_TOPO_TRIANGLE_STRIP = 4
CANONICAL_TOPO_TRIANGLE_FAN = 5

def encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_func,
                        blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                        color_blend_op=0, alpha_blend_op=0, color_mask=15,
                        cull_mode=2, topology=3, front_face_cw=0, const_alpha=0):
    """Encodes canonical pipeline key matching platform/src/gx/vk/vk_types.h exactly."""
    flags = vtx_flags
    depth_state = (1 if depth_test else 0) | ((1 if (depth_test and depth_write) else 0) << 1) | ((depth_func & 0x0F) << 4)
    blend_state = ((1 if blend_enable else 0) |
                   ((src_color & 0x1F) << 1) |
                   ((dst_color & 0x1F) << 6) |
                   ((src_alpha & 0x1F) << 11) |
                   ((dst_alpha & 0x1F) << 16))
    blend_state2 = ((color_blend_op & 7) |
                    ((alpha_blend_op & 7) << 3) |
                    ((color_mask & 0x0F) << 6) |
                    ((const_alpha & 0xFF) << 10))
    cull_val = cull_mode & 3
    prim_val = topology & 7
    front_bit = 1 if front_face_cw else 0
    raster_state = cull_val | (prim_val << 2) | (front_bit << 5)
    return (flags, depth_state, blend_state, blend_state2, raster_state)
