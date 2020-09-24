#define graphics_rootsig_main
    "CBV(b0), " \
    "StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL, filter = FILTER_ANISOTROPIC, maxAnisotropy = 8, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE), " \
    "StaticSampler(s1, visibility = SHADER_VISIBILITY_PIXEL, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc = COMPARISON_LESS_EQUAL, addressU = TEXTURE_ADDRESS_BORDER, addressV = TEXTURE_ADDRESS_BORDER, borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE), " \
    "DescriptorTable(SRV(t0, space = 0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL)"

#define imgui_rootsig
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | DENY_HULL_SHADER_ROOT_ACCESS | DENY_DOMAIN_SHADER_ROOT_ACCESS | GEOMETRY_SHADER_ROOT_ACCESS), " \
    "RootConstants(b0, num32BitConstants=16, visibility = SHADER_VISIBILITY_VERTEX),"
    "DescriptorTable(SRV(t0, space = 0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), "
    "StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL, filter = FILTER_MIN_MAG_MIP_LINEAR, maxAnisotropy = 0, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, borderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, comparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,), "