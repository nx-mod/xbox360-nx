#include "shader_recompiler.h"
#include "shader_common.h"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

static constexpr char SWIZZLES[] = 
{ 
    'x',
    'y', 
    'z', 
    'w', 
    '0', 
    '1',
    '_',
    '_'
};

static constexpr const char* USAGE_TYPES[] =
{
    "float4", // POSITION
    "float4", // BLENDWEIGHT
    "uint4", // BLENDINDICES
#ifdef NFSMW_RECOMP
    "float4", // NORMAL: NFSMW stores them as 16-bit integers or as float
#else
    "uint4", // NORMAL
#endif
    "float4", // PSIZE
    "float4", // TEXCOORD
#ifdef NFSMW_RECOMP
    "float4", // TANGENT
    "float4", // BINORMAL
#else
    "uint4", // TANGENT
    "uint4", // BINORMAL
#endif
    "float4", // TESSFACTOR
    "float4", // POSITIONT
    "float4", // COLOR
    "float4", // FOG
    "float4", // DEPTH
    "float4", // SAMPLE
};

static constexpr const char* USAGE_VARIABLES[] =
{
    "Position",
    "BlendWeight",
    "BlendIndices",
    "Normal",
    "PointSize",
    "TexCoord",
    "Tangent",
    "Binormal",
    "TessFactor",
    "PositionT",
    "Color",
    "Fog",
    "Depth",
    "Sample"
};

static constexpr const char* USAGE_SEMANTICS[] =
{
    "POSITION",
    "BLENDWEIGHT",
    "BLENDINDICES",
    "NORMAL",
    "PSIZE",
    "TEXCOORD",
    "TANGENT",
    "BINORMAL",
    "TESSFACTOR",
    "POSITIONT",
    "COLOR",
    "FOG",
    "DEPTH",
    "SAMPLE"
};

struct DeclUsageLocation
{
    DeclUsage usage;
    uint32_t usageIndex;
    uint32_t location;
};

// NOTE: These are specialized Vulkan locations for Unleashed Recompiled. Change as necessary. Likely not going to work with other games.
static constexpr DeclUsageLocation USAGE_LOCATIONS[] =
{
    { DeclUsage::Position, 0, 0 },
    { DeclUsage::Normal, 0, 1 },
    { DeclUsage::Tangent, 0, 2 },
    { DeclUsage::Binormal, 0, 3 },
    { DeclUsage::TexCoord, 0, 4 },
    { DeclUsage::TexCoord, 1, 5 },
    { DeclUsage::TexCoord, 2, 6 },
    { DeclUsage::TexCoord, 3, 7 },
    { DeclUsage::Color, 0, 8 },
    { DeclUsage::BlendIndices, 0, 9 },
    { DeclUsage::BlendWeight, 0, 10 },
    { DeclUsage::Color, 1, 11 },
    { DeclUsage::TexCoord, 4, 12 },
    { DeclUsage::TexCoord, 5, 13 },
    { DeclUsage::TexCoord, 6, 14 },
    { DeclUsage::TexCoord, 7, 15 },
    { DeclUsage::Position, 1, 15 },
};

static constexpr std::pair<DeclUsage, size_t> INTERPOLATORS[] =
{
    { DeclUsage::TexCoord, 0 },
    { DeclUsage::TexCoord, 1 },
    { DeclUsage::TexCoord, 2 },
    { DeclUsage::TexCoord, 3 },
    { DeclUsage::TexCoord, 4 },
    { DeclUsage::TexCoord, 5 },
    { DeclUsage::TexCoord, 6 },
    { DeclUsage::TexCoord, 7 },
    { DeclUsage::TexCoord, 8 },
    { DeclUsage::TexCoord, 9 },
    { DeclUsage::TexCoord, 10 },
    { DeclUsage::TexCoord, 11 },
    { DeclUsage::TexCoord, 12 },
    { DeclUsage::TexCoord, 13 },
    { DeclUsage::TexCoord, 14 },
    { DeclUsage::TexCoord, 15 },
    { DeclUsage::Color, 0 },
    { DeclUsage::Color, 1 }
#ifdef NFSMW_RECOMP
    // NFSMW declares and consumes COLOR2..5 in the original 2005 shaders.
    , { DeclUsage::Color, 2 }
    , { DeclUsage::Color, 3 }
    , { DeclUsage::Color, 4 }
    , { DeclUsage::Color, 5 }
#endif
#ifdef HALO3_RECOMP
    // Halo 3's pixel shaders also read these. Both stages declare from this one list, in this order, so the
    // implicit locations agree.
    , { DeclUsage::Normal, 0 }
    , { DeclUsage::Tangent, 0 }
    , { DeclUsage::Binormal, 0 }
#endif
};

static constexpr std::string_view TEXTURE_DIMENSIONS[] = 
{
    "2D",
    "3D", 
    "Cube" 
};

#ifdef XENOS_GLSL
// GLSL has no implicit broadcast or truncation: a value written to N components goes through the constructor of
// that size (float4 and the rest are the prelude's names for vec4...).
static const char* tipoDeComponentes(uint32_t mascara)
{
    switch (std::popcount(mascara & 0xF))
    {
    case 1: return "float";
    case 2: return "float2";
    case 3: return "float3";
    default: return "float4";
    }
}
#endif

static FetchDestinationSwizzle getDestSwizzle(uint32_t dstSwizzle, uint32_t index)
{
    return FetchDestinationSwizzle((dstSwizzle >> (index * 3)) & 0x7);
}

void ShaderRecompiler::printDstSwizzle(uint32_t dstSwizzle, bool operand)
{
    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle >= FetchDestinationSwizzle::X && swizzle <= FetchDestinationSwizzle::W)
            out += SWIZZLES[operand ? uint32_t(swizzle) : i];
    }
}

void ShaderRecompiler::printDstSwizzle01(uint32_t dstRegister, uint32_t dstSwizzle)
{
    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle == FetchDestinationSwizzle::Zero)
        {
            indent();
            println("r{}.{} = 0.0;", dstRegister, SWIZZLES[i]);
        }
        else if (swizzle == FetchDestinationSwizzle::One)
        {
            indent();
            println("r{}.{} = 1.0;", dstRegister, SWIZZLES[i]);
        }
    }
}

void ShaderRecompiler::recompile(const VertexFetchInstruction& instr, uint32_t address)
{
    if (instr.isPredicated)
        abrirPredicado(instr.predicateCondition);
    else
        cerrarPredicado();
    const size_t marcaPredicado = out.size();

    indent();
    print("r{}.", instr.dstRegister);
    printDstSwizzle(instr.dstSwizzle, false);

    out += " = ";

    auto findResult = vertexElements.find(address);
#ifdef HALO3_RECOMP
    if (findResult == vertexElements.end())
    {
        /*
         * Halo 3 reads buffers with vertex fetches that no vertex element declares: in pixel shaders (no vertex
         * input at all) and for extra streams in vertex shaders. Read as a buffer load: the fetch constant's range,
         * which the renderer copies (byte-swapped as for vertex data) and binds as storage buffer g_vf<slot>.
         * Stride and offset are in dwords; a mini fetch reuses the previous fetch's index.
         */
        out.resize(marcaPredicado);
        bool escribe = false;
        for (size_t i = 0; i < 4; i++)
        {
            const auto sw = getDestSwizzle(instr.dstSwizzle, i);
            escribe |= sw >= FetchDestinationSwizzle::X && sw <= FetchDestinationSwizzle::W;
        }
        if (!escribe)
        {
            printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);
            cerrarSiEscribePredicado(marcaPredicado);
            return;
        }
        // A mini fetch takes the fetch constant and the stride of the full fetch before it (as Xenia reads them):
        // its own fields for those are not meaningful.
        if (!instr.isMiniFetch)
        {
            ultimoSlotVf = instr.constIndex * 3 + instr.constIndexSelect;
            ultimaZancadaVf = instr.stride;
        }
        const uint32_t slot = ultimoSlotVf;
        const uint32_t zancada = ultimaZancadaVf;
        slotsVf.insert(slot);
        {
            // What each slot's fetches read around vertex i: dwords i * stride + [min, max] (with the 4 a fetch may
            // read). The renderer uploads only that range of the vertices a draw uses.
            auto [it, nuevo] = alcanceVf.try_emplace(slot, AlcanceVf{zancada, int32_t(instr.offset), int32_t(instr.offset) + 4});
            AlcanceVf& a = it->second;
            if (!nuevo)
            {
                if (a.zancada != zancada)
                    a.zancada = 0;  // mixed strides: the whole buffer
                a.minimo = std::min(a.minimo, int32_t(instr.offset));
                a.maximo = std::max(a.maximo, int32_t(instr.offset) + 4);
            }
        }
        if (!instr.isMiniFetch)
        {
            indent();
            println("vfIndice = int({}(r{}.{}));", instr.isIndexRounded ? "round" : "floor", uint32_t(instr.srcRegister),
                SWIZZLES[instr.srcSwizzle]);
        }
        indent();
        print("r{}.", uint32_t(instr.dstRegister));
        printDstSwizzle(instr.dstSwizzle, false);
#ifdef XENOS_GLSL
        print(" = halo_vfetch_{}(vfIndice * {} + ({}), {}u, {}u, {}u, {}).", slot, zancada,
#else
        print(" = halo_vfetch(g_vf{}, vfIndice * {} + ({}), {}, {}, {}, {}).", slot, zancada,
#endif
            int32_t(instr.offset), uint32_t(instr.format), uint32_t(instr.signedRfModeAll), uint32_t(instr.numFormatAll),
            int32_t(instr.expAdjust));
        printDstSwizzle(instr.dstSwizzle, true);
        out += ";\n";
        printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);
        cerrarSiEscribePredicado(marcaPredicado);
        return;
    }
#else
    if (findResult == vertexElements.end())
        throw std::runtime_error("FETCH de vertices sin elemento declarado");
#endif

#ifdef NFSMW_RECOMP
    // NFSMW: D3D patches the fetch swizzle according to the vertex declaration; the
    // native renderer passes the mapping through g_InputRemap.
    int32_t remapLocation = -1;
#ifdef HALO3_RECOMP
    {
        const uint32_t clave = (uint32_t(findResult->second.usage) << 4) | uint32_t(findResult->second.usageIndex);
        const auto it = std::find(ordenEntradas.begin(), ordenEntradas.end(), clave);
        if (it != ordenEntradas.end())
            remapLocation = int32_t(it - ordenEntradas.begin());
    }
#else
    for (auto& usageLocation : USAGE_LOCATIONS)
    {
        if (usageLocation.usage == findResult->second.usage && usageLocation.usageIndex == findResult->second.usageIndex)
        {
            remapLocation = int32_t(usageLocation.location);
            break;
        }
    }
#endif
    if (remapLocation >= 0)
        out += "remapInput(float4(";
#endif

    switch (findResult->second.usage)
    {
    // NFSMW declares normals, tangents and binormals as float4 (USAGE_TYPES): the
    // native renderer supplies them already normalized through the Vulkan format.
#ifndef NFSMW_RECOMP
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        specConstantsMask |= SPEC_CONSTANT_R11G11B10_NORMAL;
        print("tfetchR11G11B10(");
        break;
#endif

    case DeclUsage::TexCoord:
        print("tfetchTexcoord(g_SwappedTexcoords, ");
        break;
    }

    print("i{}{}", USAGE_VARIABLES[uint32_t(findResult->second.usage)], uint32_t(findResult->second.usageIndex));

    switch (findResult->second.usage)
    {
#ifndef NFSMW_RECOMP
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        out += ')';
        break;
#endif

    case DeclUsage::TexCoord:
        print(", {})", uint32_t(findResult->second.usageIndex));
        break;
    }

#ifdef NFSMW_RECOMP
    if (remapLocation >= 0)
        print("), g_InputRemap({}))", remapLocation);
#endif

    out += '.';
    printDstSwizzle(instr.dstSwizzle, true);

    out += ";\n";

    printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);

    cerrarSiEscribePredicado(marcaPredicado);
}

void ShaderRecompiler::recompile(const TextureFetchInstruction& instr, bool bicubic)
{
    if (instr.opcode != FetchOpcode::TextureFetch && instr.opcode != FetchOpcode::GetTextureWeights)
        return;

    if (instr.isPredicated)
        abrirPredicado(instr.predCondition);
    else
        cerrarPredicado();
    const size_t marcaPredicado = out.size();

    auto printSrcRegister = [&](size_t componentCount)
        {
            print("r{}.", instr.srcRegister);

            for (size_t i = 0; i < componentCount; i++)
                out += SWIZZLES[((instr.srcSwizzle >> (i * 2))) & 0x3];
        };

    std::string constName;
    const char* constNamePtr = nullptr;
#ifdef UNLEASHED_RECOMP
    bool subtractFromOne = false;
#endif

    auto findResult = samplers.find(instr.constIndex);
    if (findResult != samplers.end())
    {
        constNamePtr = findResult->second;

    #ifdef UNLEASHED_RECOMP
        subtractFromOne = hasMtxPrevInvViewProjection && strcmp(constNamePtr, "sampZBuffer") == 0;
    #endif
    }
    else
    {
        constName = fmt::format("s{}", instr.constIndex);
        constNamePtr = constName.c_str();
#ifdef HALO3_RECOMP
        // Halo 3 has no constant table: the samplers are the ones its fetches use, with the dimension each fetch
        // states. Declared at the end (see the end of recompile).
        samplersHalo.emplace(uint32_t(instr.constIndex), uint32_t(instr.dimension));
#endif
    }

#ifdef UNLEASHED_RECOMP
    if (instr.constIndex == 0 && instr.dimension == TextureDimension::Texture2D)
    {
        indent();
        print("pixelCoord = getPixelCoord({}_Texture2DDescriptorIndex, ", constNamePtr);
        printSrcRegister(2);
        out += ");\n";
    }
#endif

#ifdef HALO3_RECOMP
    {
        // A fetch that writes no component (all masked or constant 0/1): only the constants, no read.
        bool escribe = false;
        for (size_t i = 0; i < 4; i++)
        {
            const auto sw = getDestSwizzle(instr.dstSwizzle, i);
            escribe |= sw >= FetchDestinationSwizzle::X && sw <= FetchDestinationSwizzle::W;
        }
        if (!escribe)
        {
            printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);
            cerrarSiEscribePredicado(marcaPredicado);
            return;
        }
    }
#endif
    indent();
    print("r{}.", instr.dstRegister);
    printDstSwizzle(instr.dstSwizzle, false);

    out += " = ";
    switch (instr.opcode)
    {
    case FetchOpcode::TextureFetch:
    {
    #ifdef UNLEASHED_RECOMP
        if (subtractFromOne)
            out += "1.0 - ";
    #endif

        out += "tfetch";
        break;
    }
    case FetchOpcode::GetTextureWeights:
    {
        out += "getWeights";
        break;
    }
    }

    std::string_view dimension;
    uint32_t componentCount = 0;

    switch (instr.dimension)
    {
    case TextureDimension::Texture1D:
        dimension = "1D";
        componentCount = 1;
        break;
    case TextureDimension::Texture2D:
        dimension = "2D";
        componentCount = 2;
        break;
    case TextureDimension::Texture3D:
        dimension = "3D";
        componentCount = 3;
        break;
    case TextureDimension::TextureCube:
        dimension = "Cube";
        componentCount = 3;
        break;
    }

    out += dimension;

#ifdef UNLEASHED_RECOMP
    if (bicubic)
        out += "Bicubic";
#endif

    print("({0}_Texture{1}DescriptorIndex, {0}_SamplerDescriptorIndex, ", constNamePtr, dimension);
    printSrcRegister(componentCount);

    switch (instr.dimension)
    {
    case TextureDimension::Texture2D:
        // The last argument is 1/size of the slot, so the texture does not have to be queried.
        print(", float2({}, {}), {}_InvTamano", instr.offsetX * 0.5f, instr.offsetY * 0.5f, constNamePtr);
        break;
    case TextureDimension::TextureCube:
        out += ", cubeMapData";
        break;
    }

    out += ").";

    printDstSwizzle(instr.dstSwizzle, true);

    out += ";\n";

    printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);

    cerrarSiEscribePredicado(marcaPredicado);
}

void ShaderRecompiler::recompile(const AluInstruction& instr)
{
#ifdef HALO3_RECOMP
    bool soloW = false;  // op(): only the w component (MAXA)
#endif
    if (instr.isPredicated)
        abrirPredicado(instr.predicateCondition);
    else
        cerrarPredicado();
    const size_t marcaPredicado = out.size();

    enum
    {
        VECTOR_0,
        VECTOR_1,
        VECTOR_2,
        SCALAR_0,
        SCALAR_1,
        SCALAR_CONSTANT_0,
        SCALAR_CONSTANT_1
    };

    auto op = [&](size_t operand)
        {
            size_t reg = 0;
            size_t swizzle = 0;
            bool select = true;
            bool negate = false;
            bool abs = false;

            switch (operand)
            {
            case SCALAR_CONSTANT_0:
                reg = instr.src3Register;
                swizzle = instr.src3Swizzle;
                select = false;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            case SCALAR_CONSTANT_1:
                reg = (uint32_t(instr.scalarOpcode) & 1) | (instr.src3Select << 1) | (instr.src3Swizzle & 0x3C);
                swizzle = instr.src3Swizzle;
                select = true;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            default:
                switch (operand)
                {
                case VECTOR_0:
                    reg = instr.src1Register;
                    swizzle = instr.src1Swizzle;
                    select = instr.src1Select;
                    negate = instr.src1Negate;
                    break;
                case VECTOR_1:
                    reg = instr.src2Register;
                    swizzle = instr.src2Swizzle;
                    select = instr.src2Select;
                    negate = instr.src2Negate;
                    break;
                case VECTOR_2:
                case SCALAR_0:
                case SCALAR_1:
                    reg = instr.src3Register;
                    swizzle = instr.src3Swizzle;
                    select = instr.src3Select;
                    negate = instr.src3Negate;
                    break;
                }

                if (select)
                {
                    abs = (reg & 0x80) != 0;
                    reg &= 0x3F;
                }
                else
                {
                    abs = instr.absConstants;
                }

                break;
            }

            std::string regFormatted;

            if (select)
            {
                regFormatted = fmt::format("r{}", reg);
            }
            else
            {
                // The bits correspond to the first constant and the following ones,
                // not to the whole instruction. Temporary operands do not count.
                const bool relativa = operand == VECTOR_0 ? instr.const0Relative :
                    operand == VECTOR_1 ? (instr.src1Select ? instr.const0Relative : instr.const1Relative) :
                    (instr.src1Select && instr.src2Select ? instr.const0Relative : instr.const1Relative);
                auto findResult = float4Constants.find(reg);
                if (findResult != float4Constants.end())
                {
                    const char* constantName = reinterpret_cast<const char*>(constantTableData + findResult->second->name);
                    if (findResult->second->registerCount > 1)
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (hasMtxProjection && strcmp(constantName, "g_MtxProjection") == 0)
                        {
                            regFormatted = fmt::format("(iterationIndex == 0 ? mtxProjectionReverseZ[{0}] : mtxProjection[{0}])",
                                reg - findResult->second->registerIndex);
                        }
                        else
                    #endif
                        {
                            regFormatted = fmt::format("{}({}{})", constantName,
                                reg - findResult->second->registerIndex, relativa ? (instr.constAddressRegisterRelative ? " + a0" : " + aL") : "");
                        }
                    }
                    else
                    {
                        if (relativa)
                            throw std::runtime_error("direccionamiento relativo de constante escalar pendiente");
                        regFormatted = constantName;
                    }
                }
                else
                {
                    if (relativa)
                        throw std::runtime_error("direccionamiento relativo sin constante declarada pendiente");
                    regFormatted = fmt::format("c{}", reg);
                }
            }

            std::string result;

            if (negate)
                result += '-';

            if (abs)
                result += "abs(";

            result += regFormatted;
            result += '.';

            switch (operand)
            {
            case VECTOR_0:
            case VECTOR_1:
            case VECTOR_2:
            {
                uint32_t mask;

                switch (instr.vectorOpcode)
                {
                case AluVectorOpcode::Dp2Add:
                    mask = (operand == VECTOR_2) ? 0b1 : 0b11;
                    break;

                case AluVectorOpcode::Dp3:
                    mask = 0b111;
                    break;

                case AluVectorOpcode::Dp4:
                case AluVectorOpcode::Max4:
                    mask = 0b1111;
                    break;
#ifdef HALO3_RECOMP
                // SETP_*_PUSH compares one component of each source, the w (Xenia's translators), whatever the
                // write mask; with Halo's masks the full swizzle reached && and DXC refused the vectors.
                case AluVectorOpcode::SetpEqPush:
                case AluVectorOpcode::SetpNePush:
                case AluVectorOpcode::SetpGtPush:
                case AluVectorOpcode::SetpGePush:
                    mask = 0b1000;
                    break;
                // DST reads y, z of src0 and y, w of src1 whatever the write mask: full vectors, then the written
                // components of the result (below).
                case AluVectorOpcode::Dst:
                    mask = 0b1111;
                    break;
#endif

                default:
                    mask = instr.vectorWriteMask != 0 ? instr.vectorWriteMask : 0b1;
#ifdef HALO3_RECOMP
                    if (soloW)
                        mask = 0b1000;
#endif
                    break;
                }

                for (size_t i = 0; i < 4; i++)
                {
                    if ((mask >> i) & 0x1)
                        result += SWIZZLES[((swizzle >> (i * 2)) + i) & 0x3];
                }

                break;
            }

            case SCALAR_0:
            case SCALAR_CONSTANT_0:
                result += SWIZZLES[((swizzle >> 6) + 3) & 0x3];
                break;

            case SCALAR_1:
            case SCALAR_CONSTANT_1:
                result += SWIZZLES[swizzle & 0x3];
                break;
            }

            if (abs)
                result += ")";

            return result;
        };

    switch (instr.vectorOpcode)
    {
    case AluVectorOpcode::KillEq:
        indent();
#ifdef XENOS_GLSL
        println("clip(xeno_any_eq({}, {}) ? -1.0 : 1.0);", op(VECTOR_0), op(VECTOR_1));
#else
        println("clip(any({} == {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
#endif
        break;
    
    case AluVectorOpcode::KillGt:
        indent();
#ifdef XENOS_GLSL
        println("clip(xeno_any_gt({}, {}) ? -1.0 : 1.0);", op(VECTOR_0), op(VECTOR_1));
#else
        println("clip(any({} > {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
#endif
        break;
    
    case AluVectorOpcode::KillGe:
        indent();
#ifdef XENOS_GLSL
        println("clip(xeno_any_ge({}, {}) ? -1.0 : 1.0);", op(VECTOR_0), op(VECTOR_1));
#else
        println("clip(any({} >= {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
#endif
        break;
    
    case AluVectorOpcode::KillNe:
        indent();
#ifdef XENOS_GLSL
        println("clip(xeno_any_ne({}, {}) ? -1.0 : 1.0);", op(VECTOR_0), op(VECTOR_1));
#else
        println("clip(any({} != {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
#endif
        break;
    }

    bool closeIfBracket = false;

    std::string_view exportRegister;
#ifdef HALO3_RECOMP
    // Memory export (eA = 32, eM0-eM4 = 33-37): a shader writing to memory, which this translator does not do. Marked
    // so the game can say which shaders lose those writes (halo3_traductor.cpp).
    if (instr.exportData && instr.vectorDest >= 32 && instr.vectorDest <= 37)
    {
        indent();
        print("// MEMEXPORT e{}\n", uint32_t(instr.vectorDest));
    }
#endif
    if (instr.exportData)
    {
        if (isPixelShader)
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::PSColor0:
                exportRegister = "oC0";
                break;        
            case ExportRegister::PSColor1:
                exportRegister = "oC1";
                break;        
            case ExportRegister::PSColor2:
                exportRegister = "oC2";
                break;            
            case ExportRegister::PSColor3:
                exportRegister = "oC3";
                break;           
            case ExportRegister::PSDepth:
                exportRegister = "oDepth";
                break;
            }
        }
        else
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::VSPosition:
                exportRegister = "oPos";

            #ifdef UNLEASHED_RECOMP
                if (hasMtxProjection)
                {
                    indent();
                    out += "if ((g_SpecConstants() & SPEC_CONSTANT_REVERSE_Z) == 0 || iterationIndex == 0)\n";
                    indent();
                    out += "{\n";
                    ++indentation;

                    closeIfBracket = true;
                }
            #endif

                break;

            default:
            {
                auto findResult = interpolators.find(instr.vectorDest);
#ifdef HALO3_RECOMP
                // An export to an interpolator the shader does not declare: nothing reads it, it goes to a local.
                if (findResult == interpolators.end())
                {
                    exportRegister = "oDescartado";
                    break;
                }
#else
                if (findResult == interpolators.end())
                    throw std::runtime_error("exportacion de vertices sin interpolador declarado");
#endif
                exportRegister = findResult->second;
                break;
            }
            }
        }
    }

    if (instr.vectorOpcode >= AluVectorOpcode::SetpEqPush && instr.vectorOpcode <= AluVectorOpcode::SetpGePush)
    {
        indent();
        print("p0 = {} == 0.0 && {} ", op(VECTOR_0), op(VECTOR_1));

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::SetpEqPush:
            out += "==";
            break;
        case AluVectorOpcode::SetpNePush:
            out += "!=";
            break;
        case AluVectorOpcode::SetpGtPush:
            out += ">";
            break;
        case AluVectorOpcode::SetpGePush:
            out += ">=";
            break;
        }

        out += " 0.0;\n";
    }
    else if (instr.vectorOpcode >= AluVectorOpcode::MaxA)
    {
        indent();
#ifdef HALO3_RECOMP
        soloW = true;  // the w of src0 by itself: the masked swizzle may not reach w
        const std::string w = op(VECTOR_0);
        soloW = false;
        println("a0 = int(clamp(floor({} + 0.5), -256.0, 255.0));", w);
#else
        println("a0 = (int)clamp(floor(({}).w + 0.5), -256.0, 255.0);", op(VECTOR_0));
#endif
    }

    uint32_t vectorWriteMask = instr.vectorWriteMask;
    if (instr.exportData)
        vectorWriteMask &= ~instr.scalarWriteMask;
#ifdef HALO3_RECOMP
    // Vector opcodes 30 and 31 are not defined (MAXA = 29 is the last). Nothing is written; marked to find later.
    if (uint32_t(instr.vectorOpcode) > 29 && vectorWriteMask != 0)
    {
        indent();
        println("// vector opcode {} not defined: r{} left as it was", uint32_t(instr.vectorOpcode), uint32_t(instr.vectorDest));
        vectorWriteMask = 0;
    }
#endif

    if (vectorWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            out += '.';
        }
        else
        {
            print("r{}.", instr.vectorDest);
        }

        for (size_t i = 0; i < 4; i++)
        {
            if ((vectorWriteMask >> i) & 0x1)
                out += SWIZZLES[i];
        }

        out += " = ";
#ifdef XENOS_GLSL
        print("{}(", tipoDeComponentes(vectorWriteMask));
#endif

        if (instr.vectorSaturate)
            out += "saturate(";

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::Add:
            print("{} + {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Mul:
            print("{} * {}", op(VECTOR_0), op(VECTOR_1));
            break;

        /*
         * The Xenos MOV is a max with itself.
         *
         * The Xenos has no copy instruction: the assembler writes it as MAX dst, src, src
         * (and MAXs for the scalar channel). The translator emitted it as is, so the library
         * had 452 `max(a, a)` in the pixel shaders and 482 in the vertex shaders: almost a
         * thousand operations that are a copy. max(a, a) == a exactly (also with NaN and with
         * both zeros, which is the only thing that sets IEEE min/max apart), so the operand is
         * emitted directly.
         *
         * The `a0 = ...` of MaxA/MaxAs/MaxAsf is not touched: it is handled separately and
         * stays the same.
         */
        case AluVectorOpcode::Max:
        case AluVectorOpcode::MaxA:
        case AluVectorOpcode::Min:
        {
            std::string a = op(VECTOR_0);
            std::string b = op(VECTOR_1);
            if (a == b)
                out += a;
            else
                print("{}({}, {})", instr.vectorOpcode == AluVectorOpcode::Min ? "min" : "max", a, b);
            break;
        }

        case AluVectorOpcode::Seq:
#ifdef XENOS_GLSL
            print("xeno_eq({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("{} == {}", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::Sgt:
#ifdef XENOS_GLSL
            print("xeno_gt({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("{} > {}", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::Sge:
#ifdef XENOS_GLSL
            print("xeno_ge({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("{} >= {}", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::Sne:
#ifdef XENOS_GLSL
            print("xeno_ne({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("{} != {}", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::Frc:
            print("frac({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Trunc:
            print("trunc({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Floor:
            print("floor({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Mad:
            print("{} * {} + {}", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::CndEq:
#ifdef XENOS_GLSL
            print("xeno_cnd_eq({}, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#else
            print("select({} == 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#endif
            break;

        case AluVectorOpcode::CndGe:
#ifdef XENOS_GLSL
            print("xeno_cnd_ge({}, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#else
            print("select({} >= 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#endif
            break;

        case AluVectorOpcode::CndGt:
#ifdef XENOS_GLSL
            print("xeno_cnd_gt({}, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#else
            print("select({} > 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
#endif
            break;

        case AluVectorOpcode::Dp4:
        case AluVectorOpcode::Dp3:
            print("dot({}, {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Dp2Add:
            print("dot({}, {}) + {}", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::Cube:
            print("cube(r{}, cubeMapData)", instr.src1Register);
            break;

        case AluVectorOpcode::Max4:
            print("max4({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::SetpEqPush:
        case AluVectorOpcode::SetpNePush:
        case AluVectorOpcode::SetpGtPush:
        case AluVectorOpcode::SetpGePush:
            print("p0 ? 0.0 : {} + 1.0", op(VECTOR_0));
            break;

        case AluVectorOpcode::KillEq:
#ifdef XENOS_GLSL
            print("xeno_any_eq({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("any({} == {})", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::KillGt:
#ifdef XENOS_GLSL
            print("xeno_any_gt({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("any({} > {})", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::KillGe:
#ifdef XENOS_GLSL
            print("xeno_any_ge({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("any({} >= {})", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::KillNe:
#ifdef XENOS_GLSL
            print("xeno_any_ne({}, {})", op(VECTOR_0), op(VECTOR_1));
#else
            print("any({} != {})", op(VECTOR_0), op(VECTOR_1));
#endif
            break;

        case AluVectorOpcode::Dst:
#ifdef HALO3_RECOMP
        {
            print("dst({}, {}).", op(VECTOR_0), op(VECTOR_1));
            for (size_t i = 0; i < 4; i++)
                if ((vectorWriteMask >> i) & 0x1)
                    out += SWIZZLES[i];
            break;
        }
#else
            print("dst({}, {})", op(VECTOR_0), op(VECTOR_1));
            break;
#endif
#ifdef HALO3_RECOMP
        default:  // an opcode with no translation: a name DXC reports, with its number
            print("halo_op_vector_{}", uint32_t(instr.vectorOpcode));
            break;
#endif
        }

        if (instr.vectorSaturate)
            out += ')';
#ifdef XENOS_GLSL
        out += ')';
#endif

        out += ";\n";
    }

#ifdef HALO3_RECOMP
    // Scalar opcodes past 50 (RETAIN_PREV) are not defined; Halo 3's compiler leaves them in instructions whose
    // scalar half does nothing. They keep ps, as RETAIN_PREV.
    if (instr.scalarOpcode != AluScalarOpcode::RetainPrev && uint32_t(instr.scalarOpcode) <= 50 &&
        uint32_t(instr.scalarOpcode) != 41)  // 41 is the gap between SQRT and MULSC0
#else
    if (instr.scalarOpcode != AluScalarOpcode::RetainPrev)
#endif
    {
        if (instr.scalarOpcode >= AluScalarOpcode::SetpEq && instr.scalarOpcode <= AluScalarOpcode::SetpRstr)
        {
            indent();
            out += "p0 = ";

            switch (instr.scalarOpcode)
            {
            case AluScalarOpcode::SetpEq:
                print("{} == 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpNe:
                print("{} != 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpGt:
                print("{} > 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpGe:
                print("{} >= 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpInv:
                print("{} == 1.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpPop:
                print("{} - 1.0 <= 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpClr:
                out += "false";
                break;

            case AluScalarOpcode::SetpRstr:
                print("{} == 0.0", op(SCALAR_0));
                break;
            }

            out += ";\n";
        }

        indent();
#ifdef XENOS_GLSL
        out += "ps = float(";  // comparisons give bool
#else
        out += "ps = ";
#endif
        if (instr.scalarSaturate)
            out += "saturate(";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::Adds:
            print("{} + {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::AddsPrev:
            print("{} + ps", op(SCALAR_0));
            break;

        case AluScalarOpcode::Muls:
            print("{} * {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::MulsPrev:
        case AluScalarOpcode::MulsPrev2:
            print("{} * ps", op(SCALAR_0));
            break;

        // Same as above, for the scalar channel: MAXs dst, src, src is the Xenos MOV.
        case AluScalarOpcode::Maxs:
        case AluScalarOpcode::MaxAs:
        case AluScalarOpcode::MaxAsf:
        case AluScalarOpcode::Mins:
        {
            std::string a = op(SCALAR_0);
            std::string b = op(SCALAR_1);
            if (a == b)
                out += a;
            else
                print("{}({}, {})", instr.scalarOpcode == AluScalarOpcode::Mins ? "min" : "max", a, b);
            break;
        }

        case AluScalarOpcode::Seqs:
            print("{} == 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sgts:
            print("{} > 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sges:
            print("{} >= 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Snes:
            print("{} != 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Frcs:
            print("frac({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Truncs:
            print("trunc({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Floors:
            print("floor({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Exp:
            print("exp2({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Logc:
        case AluScalarOpcode::Log:
            print("clamp(log2({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Rcpc:
        case AluScalarOpcode::Rcpf:
        case AluScalarOpcode::Rcp:
            print("clamp(rcp({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Rsqc:
        case AluScalarOpcode::Rsqf:
        case AluScalarOpcode::Rsq:
            print("clamp(rsqrt({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Subs:
            print("{} - {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::SubsPrev:
            print("{} - ps", op(SCALAR_0));
            break;

        case AluScalarOpcode::SetpEq:
        case AluScalarOpcode::SetpNe:
        case AluScalarOpcode::SetpGt:
        case AluScalarOpcode::SetpGe:
            out += "p0 ? 0.0 : 1.0";
            break;

        case AluScalarOpcode::SetpInv:
#ifdef NFSMW_RECOMP
            // Xenos: with src == 1 the counter goes back to 0 (Xenia's ucode.h, kSetpInv). Without that
            // case the counter stayed at 1 and later p0 blocks did not run.
            print("p0 ? 0.0 : ({0} == 0.0 ? 1.0 : {0})", op(SCALAR_0));
#else
            print("{0} == 0.0 ? 1.0 : {0}", op(SCALAR_0));
#endif
            break;

        case AluScalarOpcode::SetpPop:
            print("p0 ? 0.0 : ({} - 1.0)", op(SCALAR_0));
            break;

        case AluScalarOpcode::SetpClr:
            out += "FLT_MAX";
            break;

        case AluScalarOpcode::SetpRstr:
            print("p0 ? 0.0 : {}", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsEq:
            print("{} == 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsGt:
            print("{} > 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsGe:
            print("{} >= 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsNe:
            print("{} != 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsOne:
            print("{} == 1.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sqrt:
            print("sqrt({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Mulsc0:
        case AluScalarOpcode::Mulsc1:
            print("{} * {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Addsc0:
        case AluScalarOpcode::Addsc1:
            print("{} + {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Subsc0:
        case AluScalarOpcode::Subsc1:
            print("{} - {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Sin:
            print("sin({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Cos:
            print("cos({})", op(SCALAR_0));
            break;
#ifdef HALO3_RECOMP
        case AluScalarOpcode::RetainPrev:  // keeps the previous scalar result
            out += "ps";
            break;
        default:  // an opcode with no translation: a name DXC reports, with its number
            print("halo_op_escalar_{}", uint32_t(instr.scalarOpcode));
            break;
#endif
        }

        if (instr.scalarSaturate)
            out += ')';
#ifdef XENOS_GLSL
        out += ')';
#endif

        out += ";\n";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::MaxAs:
            indent();
            #ifdef XENOS_GLSL
            println("a0 = int(clamp(floor({} + 0.5), -256.0, 255.0));", op(SCALAR_0));
#else
            println("a0 = (int)clamp(floor({} + 0.5), -256.0, 255.0);", op(SCALAR_0));
#endif
            break;     
        case AluScalarOpcode::MaxAsf:
            indent();
            #ifdef XENOS_GLSL
            println("a0 = int(clamp(floor({}), -256.0, 255.0));", op(SCALAR_0));
#else
            println("a0 = (int)clamp(floor({}), -256.0, 255.0);", op(SCALAR_0));
#endif
            break;
        }
    }

    uint32_t scalarWriteMask = instr.scalarWriteMask;
    if (instr.exportData)
        scalarWriteMask &= ~instr.vectorWriteMask;

    if (scalarWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            out += '.';
        }
        else
        {
            print("r{}.", instr.scalarDest);
        }

        for (size_t i = 0; i < 4; i++)
        {
            if ((scalarWriteMask >> i) & 0x1)
                out += SWIZZLES[i];
        }

#ifdef XENOS_GLSL
        print(" = {}(ps);\n", tipoDeComponentes(scalarWriteMask));
#else
        out += " = ps;\n";
#endif
    }

#ifdef HALO3_RECOMP
    // An export to a target with no name here (the point size / edge flag one): its constant writes go nowhere.
    if (instr.exportData && !exportRegister.empty())
#else
    if (instr.exportData)
#endif
    {
        uint32_t zeroMask = instr.scalarDestRelative ? (0b1111 & ~(instr.vectorWriteMask | instr.scalarWriteMask)) : 0;
        uint32_t oneMask = instr.vectorWriteMask & instr.scalarWriteMask;

        for (size_t i = 0; i < 4; i++)
        {
            uint32_t mask = 1 << i;
            if (zeroMask & mask)
            {
                indent();
                println("{}.{} = 0.0;", exportRegister, SWIZZLES[i]);
            }
            else if (oneMask & mask)
            {
                indent();
                println("{}.{} = 1.0;", exportRegister, SWIZZLES[i]);
            }
        }
    }

    if (instr.scalarOpcode >= AluScalarOpcode::KillsEq && instr.scalarOpcode <= AluScalarOpcode::KillsOne)
    {
        indent();
        out += "clip(ps != 0.0 ? -1 : 1);\n";
    }

    if (closeIfBracket)
    {
        --indentation;
        indent();
        out += "}\n";
    }

    cerrarSiEscribePredicado(marcaPredicado);
}

#ifdef NFSMW_RECOMP
void ShaderRecompiler::printDeko3dConstants(const ConstantTableContainer* constantTableContainer)
{
    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
#ifdef HALO3_RECOMP
                uint32_t tailCount = 256 - constantInfo->registerIndex;  // Halo 3's pixel shaders read up to c255
#else
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;
#endif
                println("#define {}(INDEX) select((INDEX) < {}, g_Ubo{}.v[{} + min(INDEX, {})], 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} g_Ubo{}.v[{}]", constantName, shaderName, constantInfo->registerIndex.get());
            }
            break;
        }

        case RegisterSet::Sampler:
        {
#ifdef HALO3_RECOMP
            // Halo 3: samplers come from the fetches (their own dimension), table or not. A table's generic
            // "Sampler" type said 2D for a 3D fetch.
            break;
#endif
            // One texture per register with the type the constant table gives it (as the renderer reads it:
            // nfsmw_nativo_shaders.cpp), at binding R; its sampler at binding R too (another set: GLSL's
            // combined sampler takes the texture's binding, nfsmw_dk_glsl.cpp). deko3d has 32 texture
            // bindings per stage: 0-15 for the registers and 16-31 for the pairs of the 2D ones.
            const uint32_t reg = constantInfo->registerIndex;
            const auto typeInfo = reinterpret_cast<const TypeInfo*>(constantTableData + constantInfo->typeInfo);
            const char* dimension = "2D";
            switch (typeInfo->parameterType)
            {
            case ParameterType::Texture3D:
            case ParameterType::Sampler3D:
                dimension = "3D";
                break;
            case ParameterType::TextureCube:
            case ParameterType::SamplerCube:
                dimension = "Cube";
                break;
            default:
                break;
            }

            if (reg >= 16)
                println("#error \"{}: sampler register {} has no deko3d binding (0-15)\"", constantName, reg);

            println("[[vk::binding({}, 0)]] Texture{}<float4> {}_Textura;", reg, dimension, constantName);
            println("[[vk::binding({}, 1)]] SamplerState {}_Muestreo;", reg, constantName);
            println("#define {0}_Texture{1}DescriptorIndex {0}_Textura", constantName, dimension);
            println("#define {0}_SamplerDescriptorIndex {0}_Muestreo", constantName);
            if (strcmp(dimension, "2D") == 0)
            {
                // tfetch2DSombraMin's pair arrives in the 3D index word of the same register.
                println("[[vk::binding({}, 0)]] Texture2D<float4> {}_Pareja;", 16 + reg, constantName);
                println("#define {0}_Texture3DDescriptorIndex {0}_Pareja", constantName);
            }

            const uint32_t invBase = 360 + reg * 8;
            println("#define {}_InvTamano float2(NFSMW_COMPARTIDA_FLOAT({}), NFSMW_COMPARTIDA_FLOAT({}))",
                constantName, invBase, invBase + 4);
            break;
        }

        default:
            break;
        }
    }
}
#endif

void ShaderRecompiler::recompile(const uint8_t* shaderData, const std::string_view& include)
{
    const auto shaderContainer = reinterpret_cast<const ShaderContainer*>(shaderData);

    /*
     * Most Wanted (2005) has 0x102A0E00; Unleashed (2008), 0x102A1100.
     * Both are accepted: only the two high bytes are compared.
     */
    assert((shaderContainer->flags & 0xFFFF0000) == 0x102A0000);
    assert(shaderContainer->constantTableOffset != NULL);

    out += include;
    out += '\n';

    isPixelShader = (shaderContainer->flags & 0x1) == 0;
#ifdef HALO3_RECOMP
    if (!isPixelShader)
        out.insert(0, "#define HALO3_VS 1\n");  // texture fetches without implicit LOD (shader_common.h NFSMW_SAMPLE)
#endif

    const auto constantTableContainer = reinterpret_cast<const ConstantTableContainer*>(shaderData + shaderContainer->constantTableOffset);
    constantTableData = reinterpret_cast<const uint8_t*>(&constantTableContainer->constantTable);

    out += "#ifdef __spirv__\n\n";

#ifdef NFSMW_RECOMP
    // NFSMW_DEKO3D (shader_common.h): the same constants for deko3d, written after this block. Without the
    // define DXC sees exactly what it saw before that mode existed.
    out += "#ifndef NFSMW_DEKO3D\n\n";
#endif

#ifdef UNLEASHED_RECOMP
    bool isMetaInstancer = false;
    bool hasIndexCount = false;
#endif

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

    #ifdef UNLEASHED_RECOMP
        if (!isPixelShader)
        {
            if (strcmp(constantName, "g_MtxProjection") == 0)
                hasMtxProjection = true;
            else if (strcmp(constantName, "g_InstanceTypes") == 0)
                isMetaInstancer = true;
            else if (strcmp(constantName, "g_IndexCount") == 0)
                hasIndexCount = true;
        }
        else
        {
            if (strcmp(constantName, "g_MtxPrevInvViewProjection") == 0)
                hasMtxPrevInvViewProjection = true;
        }
    #endif

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;

                // NFSMW: dynamic UBO or pointer, depending on SPEC_CONSTANT_CONSTANTES_UBO.
                println("#define {}(INDEX) select((INDEX) < {}, (NFSMW_UBO ? g_Ubo{}.v[{} + min(INDEX, {})] : vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + ({} + min(INDEX, {})) * 16, 0x10)), 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1,
                    shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} (NFSMW_UBO ? g_Ubo{}.v[{}] : vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + {}, 0x10))",
                    constantName, shaderName, constantInfo->registerIndex.get(), shaderName, constantInfo->registerIndex * 16);
            }
            
            for (uint16_t j = 0; j < constantInfo->registerCount; j++)
                float4Constants.emplace(constantInfo->registerIndex + j, constantInfo);

            break;
        }

        case RegisterSet::Sampler:
        {
#ifdef HALO3_RECOMP
            break;  // see printDeko3dConstants: the fetches declare them (s<slot>)
#endif
            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex (NFSMW_UBO ? NFSMW_COMPARTIDA_UINT({}) : vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {}))",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4, j * 64 + constantInfo->registerIndex * 4);
            }

            println("#define {}_SamplerDescriptorIndex (NFSMW_UBO ? NFSMW_COMPARTIDA_UINT({}) : vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {}))",
                constantName, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4);

            // 1/size of the host image of that slot, which the renderer writes at byte
            // 360 + slot * 8 of the shared constants (right after g_InputRemap).
            {
                const uint32_t invBase = 360 + constantInfo->registerIndex * 8;
                println("#define {}_InvTamano (NFSMW_UBO ? float2(NFSMW_COMPARTIDA_FLOAT({}), NFSMW_COMPARTIDA_FLOAT({})) : vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + {}))",
                    constantName, invBase, invBase + 4, invBase);
            }

            samplers.emplace(constantInfo->registerIndex, constantName);
            break;
        }

        }
    }

#ifdef NFSMW_RECOMP
    out += "\n#else\n\n";
    printDeko3dConstants(constantTableContainer);
    out += "\n#endif\n";
#endif

    out += "\n#else\n\n";

    println("cbuffer {}ShaderConstants : register(b{}, space4)", isPixelShader ? "Pixel" : "Vertex", isPixelShader ? 1 : 0);
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Float4)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            print("\tfloat4 {}", constantName);

            if (constantInfo->registerCount > 1)
                print("[{}]", constantInfo->registerCount.get());

            println(" : packoffset(c{});", constantInfo->registerIndex.get());

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;
                println("#define {0}(INDEX) select((INDEX) < {1}, {0}[min(INDEX, {2})], 0.0)", constantName, tailCount, tailCount - 1);
            }
        }
    }

    out += "};\n\n";

    out += "cbuffer SharedConstants : register(b2, space4)\n";
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Sampler)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("\tuint {}_Texture{}DescriptorIndex : packoffset(c{}.{});",
                    constantName, TEXTURE_DIMENSIONS[j], j * 4 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
            }

            println("\tuint {}_SamplerDescriptorIndex : packoffset(c{}.{});",
                constantName, 4 * std::size(TEXTURE_DIMENSIONS) + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
        }
    }

    out += "\tDEFINE_SHARED_CONSTANTS();\n";
    out += "};\n\n";

    out += "#endif\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Bool)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);
            println("\t#define {} (1 << {})", constantName, constantInfo->registerIndex + (isPixelShader ? 16 : 0));
            // In NFSMW CTAB numbers b0.. and CF uses 128.. for pixels.
            // The g_Booleans packing keeps its 16 bits per stage.
#ifdef NFSMW_RECOMP
            boolConstants.emplace(constantInfo->registerIndex + (isPixelShader ? 128 : 0), constantName);
#else
            boolConstants.emplace(constantInfo->registerIndex, constantName);
#endif
        }
    }

    out += '\n';

    const auto shader = reinterpret_cast<const Shader*>(shaderData + shaderContainer->shaderOffset);

#ifdef HALO3_RECOMP
    marcaBuffersVf = out.size();  // the g_vf buffers are declared here once the body has been written
#endif
    out += "#ifndef __spirv__\n";

    if (isPixelShader)
        out += "[shader(\"pixel\")]\n";
    else
        out += "[shader(\"vertex\")]\n";

    out += "#endif\n";

#ifdef XENOS_GLSL
    // GLSL: inputs and outputs are globals at explicit locations and main takes nothing. Interpolators sit at their
    // index in INTERPOLATORS (TEXCOORDn = n, COLORn = 16 + n, as the renderer counts them); vertex inputs in
    // element order, as for HLSL (HALO3_RECOMP).
    // With soloInterpoladoresDelContenedor, which entries of INTERPOLATORS the container lists.
    std::vector<bool> declarado(std::size(INTERPOLATORS), !soloInterpoladoresDelContenedor);
    if (soloInterpoladoresDelContenedor)
    {
        const uint32_t cuantos = (shader->interpolatorInfo >> 5) & 0x1F;
        for (uint32_t i = 0; i < cuantos; i++)
        {
            union
            {
                Interpolator interpolator;
                uint32_t value;
            };
            if (isPixelShader)
                value = reinterpret_cast<const PixelShader*>(shader)->interpolators[i];
            else
            {
                auto vs = reinterpret_cast<const VertexShader*>(shader);
                value = vs->vertexElementsAndInterpolators[vs->field18 + vs->vertexElementCount + i];
            }
            for (size_t k = 0; k < std::size(INTERPOLATORS); k++)
                if (INTERPOLATORS[k].first == interpolator.usage && INTERPOLATORS[k].second == interpolator.usageIndex)
                    declarado[k] = true;
        }
    }
    interpoladoresDeclarados = declarado;
    if (isPixelShader)
    {
        out += "#define iPos gl_FragCoord\n#define iFace gl_FrontFacing\n";
        for (size_t k = 0; k < std::size(INTERPOLATORS); k++)
            if (declarado[k])
                println("layout(location = {}) in float4 i{}{};", k, USAGE_VARIABLES[uint32_t(INTERPOLATORS[k].first)],
                    INTERPOLATORS[k].second);
        auto pixelShader = reinterpret_cast<const PixelShader*>(shader);
        for (uint32_t c = 0; c < 4; c++)
            if (pixelShader->outputs & (PIXEL_SHADER_OUTPUT_COLOR0 << c))
                println("layout(location = {0}) out float4 oC{0};", c);
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
            out += "#define oDepth gl_FragDepth\n";
    }
    else
    {
        auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
        for (uint32_t i = 0; i < vertexShader->vertexElementCount; i++)
        {
            union
            {
                VertexElement vertexElement;
                uint32_t value;
            };
            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + i];
            vertexElements.emplace(uint32_t(vertexElement.address), vertexElement);
            const uint32_t clave = (uint32_t(vertexElement.usage) << 4) | uint32_t(vertexElement.usageIndex);
            if (std::find(ordenEntradas.begin(), ordenEntradas.end(), clave) != ordenEntradas.end())
                continue;
            println("layout(location = {}) in {} i{}{};", ordenEntradas.size(), USAGE_TYPES[uint32_t(vertexElement.usage)],
                USAGE_VARIABLES[uint32_t(vertexElement.usage)], uint32_t(vertexElement.usageIndex));
            ordenEntradas.push_back(clave);
        }
        out += "#define oPos gl_Position\n";
        for (size_t k = 0; k < std::size(INTERPOLATORS); k++)
            if (declarado[k])
                println("layout(location = {}) out float4 o{}{};", k, USAGE_VARIABLES[uint32_t(INTERPOLATORS[k].first)],
                    INTERPOLATORS[k].second);
    }
    out += "void main()\n";
#else
    out += "void main(\n";

    if (isPixelShader)
    {
        out += "\tin float4 iPos : SV_Position,\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\tin float4 i{0}{1} : {2}{1},", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#ifdef __spirv__\n";
        out += "\tin bool iFace : SV_IsFrontFace\n";
        out += "#else\n";
        out += "\tin uint iFace : SV_IsFrontFace\n";
        out += "#endif\n";

        auto pixelShader = reinterpret_cast<const PixelShader*>(shader);
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR0)
            out += ",\n\tout float4 oC0 : SV_Target0";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR1)
            out += ",\n\tout float4 oC1 : SV_Target1";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR2)
            out += ",\n\tout float4 oC2 : SV_Target2";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR3)
            out += ",\n\tout float4 oC3 : SV_Target3";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
            out += ",\n\tout float oDepth : SV_Depth";
    }
    else
    {
        auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
        for (uint32_t i = 0; i < vertexShader->vertexElementCount; i++)
        {
            union
            {
                VertexElement vertexElement;
                uint32_t value;
            };

            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + i];

            const char* usageType = USAGE_TYPES[uint32_t(vertexElement.usage)];

        #ifdef UNLEASHED_RECOMP
            if ((vertexElement.usage == DeclUsage::TexCoord && vertexElement.usageIndex == 2 && isMetaInstancer) ||
                (vertexElement.usage == DeclUsage::Position && vertexElement.usageIndex == 1))
            {
                usageType = "uint4";
            }
        #endif

#ifdef HALO3_RECOMP
            // Halo 3: every distinct (usage, index) takes the next location, in the order of the element list (its
            // skinned meshes use more usages than the 16-entry table knows). The renderer applies the same rule
            // (EntradaShader::ubicaciones_por_orden). An element repeated in the list is declared once.
            {
                const uint32_t clave = (uint32_t(vertexElement.usage) << 4) | uint32_t(vertexElement.usageIndex);
                if (std::find(ordenEntradas.begin(), ordenEntradas.end(), clave) != ordenEntradas.end())
                {
                    vertexElements.emplace(uint32_t(vertexElement.address), vertexElement);
                    continue;
                }
                print("\t[[vk::location({})]] ", ordenEntradas.size());
                ordenEntradas.push_back(clave);
            }
#else
            out += '\t';

            for (auto& usageLocation : USAGE_LOCATIONS)
            {
                if (usageLocation.usage == vertexElement.usage && usageLocation.usageIndex == vertexElement.usageIndex)
                {
                    print("[[vk::location({})]] ", usageLocation.location);
                    break;
                }
            }
#endif

            println("in {0} i{1}{2} : {3}{2},", usageType, USAGE_VARIABLES[uint32_t(vertexElement.usage)],
                uint32_t(vertexElement.usageIndex), USAGE_SEMANTICS[uint32_t(vertexElement.usage)]);

            vertexElements.emplace(uint32_t(vertexElement.address), vertexElement);
        }

    #ifdef UNLEASHED_RECOMP
        if (hasIndexCount)
        {
            out += "\tin uint iVertexId : SV_VertexID,\n";
            out += "\tin uint iInstanceId : SV_InstanceID,\n";
        }
    #endif

    #ifdef HALO3_RECOMP
        out += "\tin uint iVertexId : SV_VertexID,\n";  // r0.x (XENO_INDICE_VERTICE)
    #endif
        out += "\tout float4 oPos : SV_Position";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            print(",\n\tout float4 o{0}{1} : {2}{1}", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);
    }

    out += ")\n";
#endif
    out += "{\n";

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
    {
        specConstantsMask |= SPEC_CONSTANT_REVERSE_Z;

        out += "\toPos = 0.0;\n";

        out += "\tfloat4x4 mtxProjection = float4x4(g_MtxProjection(0), g_MtxProjection(1), g_MtxProjection(2), g_MtxProjection(3));\n";
        out += "\tfloat4x4 mtxProjectionReverseZ = mul(mtxProjection, float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 1, 1));\n";

        out += "\t[unroll] for (int iterationIndex = 0; iterationIndex < 2; iterationIndex++)\n";
        out += "\t{\n";
    }
#endif

    if (shaderContainer->definitionTableOffset != NULL)
    {
        auto definitionTable = reinterpret_cast<const DefinitionTable*>(shaderData + shaderContainer->definitionTableOffset);
        auto definitions = definitionTable->definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Float4Definition*>(definitions);
            auto value = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + definition->physicalOffset);
            for (uint16_t i = 0; i < (definition->count + 3) / 4; i++)
            {
                println("\tfloat4 c{} = asfloat(uint4(0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}));",
                    definition->registerIndex + i - (isPixelShader ? 256 : 0), value[0].get(), value[1].get(), value[2].get(), value[3].get());

                value += 4;
            }
            definitions += 2;
        }
        ++definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Int4Definition*>(definitions);
            for (uint16_t i = 0; i < definition->count; i++)
            {
                /*
                 * Named: GCC does not accept the anonymous local union member
                 * that MSVC and clang accept with -fms-extensions.
                 */
                union Bytes4
                {
                    uint32_t value;
                    struct { int8_t x, y, z, w; } b;
                } u;

                u.value = definition->values[i].get();

#ifdef HALO3_RECOMP
                enterosDefinidos.insert((definition->registerIndex - 8992) / 4 + i);
#endif
                println("\tint4 i{} = int4({}, {}, {}, {});",
                    (definition->registerIndex - 8992) / 4 + i, u.b.x, u.b.y, u.b.z, u.b.w);
            }
            definitions += 2;
            definitions += definition->count;
        }

        out += "\n";
    }

#ifdef HALO3_RECOMP
    // Halo 3 uses temporaries past r31 (r64 and up seen); the Xenos has 128. NFS keeps its 32.
    constexpr size_t kTemporales = 128;
#else
    constexpr size_t kTemporales = 32;
#endif
    bool printedRegisters[kTemporales]{};

    uint32_t interpolatorCount = (shader->interpolatorInfo >> 5) & 0x1F;

    for (uint32_t i = 0; i < interpolatorCount; i++)
    {
        union
        {
            Interpolator interpolator;
            uint32_t value;
        };
    
        if (isPixelShader)
        {
            value = reinterpret_cast<const PixelShader*>(shader)->interpolators[i];
            println("\tfloat4 r{} = i{}{};", uint32_t(interpolator.reg), USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex));
            printedRegisters[interpolator.reg] = true;
        }
        else
        {
            auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + vertexShader->vertexElementCount + i];
            interpolators.emplace(i, fmt::format("o{}{}", USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex)));
        }
    }

    if (!isPixelShader)
    {
    #ifdef UNLEASHED_RECOMP
        if (!hasMtxProjection)
            out += "\toPos = 0.0;\n";
    #endif

        for (size_t k = 0; k < std::size(INTERPOLATORS); k++)
#ifdef XENOS_GLSL
            if (interpoladoresDeclarados.empty() || interpoladoresDeclarados[k])
                println("\to{}{} = float4(0.0);", USAGE_VARIABLES[uint32_t(INTERPOLATORS[k].first)],
                    INTERPOLATORS[k].second);
#else
            println("\to{}{} = 0.0;", USAGE_VARIABLES[uint32_t(INTERPOLATORS[k].first)], INTERPOLATORS[k].second);
#endif

        out += "\n";
    }

    for (size_t i = 0; i < kTemporales; i++)
    {
        if (!printedRegisters[i])
        {
            print("\tfloat4 r{} = ", i);
            if (isPixelShader && i == ((shader->fieldC >> 8) & 0xFF))
            {
                out += "float4((iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);\n";
            }
        #ifdef HALO3_RECOMP
            // The vertex index, which the hardware loads into r0.x: the element-less fetches read their buffers by it.
            else if (!isPixelShader && i == 0)
            {
                out += "float4(float(XENO_INDICE_VERTICE), 0.0, 0.0, 0.0);\n";
            }
        #endif
        #ifdef UNLEASHED_RECOMP
            else if (!isPixelShader && hasIndexCount && i == 0)
            {
                out += "float4(iVertexId + g_IndexCount.x * iInstanceId, 0.0, 0.0, 0.0);\n";
            }
        #endif
            else
            {
#ifdef XENOS_GLSL
                out += "float4(0.0);\n";
#else
                out += "0.0;\n";
#endif
            }
        }
    }

    out += "\tint a0 = 0;\n";
#ifdef HALO3_RECOMP
    out += "\tint vfIndice = 0;\n";
#ifdef XENOS_GLSL
    out += "\tfloat4 oDescartado = float4(0.0);\n";
#else
    out += "\tfloat4 oDescartado = 0.0;\n";
#endif
    // Loop counters the definition table does not give come from the GPU's loop constants (halo_loop).
    for (uint32_t i = 0; i < 32; i++)
        if (!enterosDefinidos.count(i))
            println("\tint4 i{0} = halo_loop({0});", i);
#endif
    out += "\tint aL = 0;\n";
    out += "\tbool p0 = false;\n";
    out += "\tfloat ps = 0.0;\n";
    if (isPixelShader)
    {
#ifdef UNLEASHED_RECOMP
        out += "\tfloat2 pixelCoord = 0.0;\n";
#endif
#ifdef XENOS_GLSL
        out += "\tCubeMapData cubeMapData = xeno_cubo_vacio();\n";
#else
        out += "\tCubeMapData cubeMapData = (CubeMapData)0;\n";
#endif
    }
#ifdef HALO3_RECOMP
    else
#ifdef XENOS_GLSL
        out += "\tCubeMapData cubeMapData = xeno_cubo_vacio();\n";  // Halo's vertex shaders fetch cubemaps too
#else
        out += "\tCubeMapData cubeMapData = (CubeMapData)0;\n";  // Halo's vertex shaders fetch cubemaps too
#endif
#endif

    const be<uint32_t>* code = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + shader->physicalOffset);

    /*
     * Named, with references: GCC does not accept the anonymous struct inside a local
     * union that MSVC and clang accept. The references leave the rest of the body
     * untouched, which uses code0..code3 eighteen times.
     */
    union ControlFlowWords
    {
        ControlFlowInstruction controlFlow[2];
        struct { uint32_t code0, code1, code2, code3; } w;
    } cfu;
    uint32_t& code0 = cfu.w.code0;
    uint32_t& code1 = cfu.w.code1;
    uint32_t& code2 = cfu.w.code2;
    uint32_t& code3 = cfu.w.code3;

    auto controlFlowCode = code;
    uint32_t instrAddress = 0;
    uint32_t instrSize = shader->size;
    bool simpleControlFlow = true;
#ifdef HALO3_RECOMP
    // Halo 3: the simple (structured) form closes a forward jump's block at its target. A target at or before the
    // jump, or a jump together with loops (its target can be past the loop's end), interleaves the braces; those
    // shaders take the general form (switch on pc), which is right for any control flow.
    uint32_t pcPrimeraPasada = 0;
    bool hayBucle = false, haySalto = false;
    // Forward jumps as (pc, target): conditional ones open an `if` that closes at the target; an unconditional one
    // that is the last instruction before such a target is that if's `else`, which closes at its own target.
    std::vector<std::pair<uint32_t, uint32_t>> saltosSi, saltosIncondicionales;
    // Loops as [LoopStart, LoopEnd + 1): structured (a `for`) as long as no jump crosses their edges.
    std::vector<std::pair<uint32_t, uint32_t>> bucles;
    std::vector<uint32_t> buclesAbiertos;
#endif

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : cfu.controlFlow)
        {
            uint32_t address = 0;

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                break;

            case ControlFlowOpcode::CondJmp:
            {
#ifdef HALO3_RECOMP
                // An unconditional forward jump may be an if/else (decided after this pass, see saltosSi).
                if (cfInstr.condJmp.isUnconditional && !cfInstr.condJmp.direction &&
                    cfInstr.condJmp.address > pcPrimeraPasada)
                    saltosIncondicionales.push_back({pcPrimeraPasada, uint32_t(cfInstr.condJmp.address)});
                else
#endif
                if (cfInstr.condJmp.isUnconditional || cfInstr.condJmp.direction)
                    simpleControlFlow = false;
                else
                {
                    ++ifEndLabels[cfInstr.condJmp.address];
#ifdef HALO3_RECOMP
                    saltosSi.push_back({pcPrimeraPasada, uint32_t(cfInstr.condJmp.address)});
#endif
                }
#ifdef HALO3_RECOMP
                haySalto = true;
                if (cfInstr.condJmp.address <= pcPrimeraPasada)
                    simpleControlFlow = false;
#endif
                break;
            }
#ifdef HALO3_RECOMP
            case ControlFlowOpcode::LoopStart:
                hayBucle = true;
                buclesAbiertos.push_back(pcPrimeraPasada);
                break;
            case ControlFlowOpcode::LoopEnd:
                hayBucle = true;
                // Only start/end pairs are loops here: an unpaired marker (the scan can read past the control flow
                // into instruction words) structures nothing, as before.
                if (!buclesAbiertos.empty())
                {
                    bucles.push_back({buclesAbiertos.back(), pcPrimeraPasada + 1});
                    buclesAbiertos.pop_back();
                }
                break;
#endif
            }

            if (address != 0)
                instrSize = std::min<uint32_t>(instrSize, address * 12);
#ifdef HALO3_RECOMP
            ++pcPrimeraPasada;
#endif
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

#ifdef HALO3_RECOMP
    if (std::getenv("XENOS_MOTIVO_CF") && (!simpleControlFlow || (hayBucle && haySalto) || !saltosIncondicionales.empty()))
        std::fprintf(stderr, "[cf] %s: simple antes=%d bucle=%d saltos_si=%zu incondicionales=%zu\n",
                     isPixelShader ? "ps" : "vs", int(simpleControlFlow), int(hayBucle), saltosSi.size(),
                     saltosIncondicionales.size());
    /*
     * The if/else shape D3D compiles: `cond_jmp T1` (skip the then part), the then part, `jmp T2` as the last
     * instruction before T1 (skip the else part), the else part, T2. Accepted only when T1 closes that one `if` and
     * every region nests in every other (then [c, T1), else [T1, T2), and the other ifs): anything else keeps the
     * general form, which is right for any control flow but which the GPU compilers handle badly (Halo 3's
     * vertex shaders with it reached 155 registers).
     */
#ifdef XENOS_GLSL
    if (!buclesConSaltosEstructurados && hayBucle && haySalto)
        simpleControlFlow = false;
#endif
    if (simpleControlFlow && (!saltosIncondicionales.empty() || (hayBucle && haySalto)))
    {
        std::vector<std::pair<uint32_t, uint32_t>> elses;
        for (auto [u, t2] : saltosIncondicionales)
        {
            const uint32_t t1 = u + 1;
            const auto si = std::find_if(saltosSi.begin(), saltosSi.end(), [&](auto j) { return j.second == t1 && j.first < u; });
            const auto etiqueta = ifEndLabels.find(t1);
            if (si == saltosSi.end() || etiqueta == ifEndLabels.end() || etiqueta->second != 1 || t2 <= t1 ||
                elsesEn.count(t1))
            {
                simpleControlFlow = false;
                break;
            }
            elses.push_back({t1, t2});
        }
        // The braces as the structured form writes them, simulated: at each pc first the `if`s whose label it is
        // close (an if/else's if turns into its else, which closes at T2), then its instruction opens a loop, closes
        // one or opens an `if`. Every close must find its own brace on top; anything else keeps the general form.
        if (simpleControlFlow)
        {
            struct Abierto { bool bucle; uint32_t fin; };
            std::vector<Abierto> pila;
            std::map<uint32_t, std::vector<uint32_t>> siEn;
            std::set<uint32_t> abreBucle, cierraBucle;
            for (auto [c, t] : saltosSi)
                siEn[c].push_back(t);
            for (auto [a, e] : bucles)
            {
                abreBucle.insert(a);
                cierraBucle.insert(e - 1);
            }
            std::map<uint32_t, uint32_t> elseDe(elses.begin(), elses.end());
            for (uint32_t p = 0; simpleControlFlow && p <= pcPrimeraPasada; p++)
            {
                while (!pila.empty() && !pila.back().bucle && pila.back().fin == p)
                {
                    if (const auto e = elseDe.find(p); e != elseDe.end())
                    {
                        pila.back().fin = e->second;
                        elseDe.erase(e);
                        break;
                    }
                    pila.pop_back();
                }
                for (const Abierto& a : pila)
                    if (!a.bucle && a.fin == p)
                        simpleControlFlow = false;  // its label comes while a brace opened after it is still open
                if (abreBucle.count(p))
                    pila.push_back({true, 0});
                if (cierraBucle.count(p))
                {
                    if (pila.empty() || !pila.back().bucle)
                        simpleControlFlow = false;
                    else
                        pila.pop_back();
                }
                if (const auto si = siEn.find(p); si != siEn.end())
                    for (uint32_t t : si->second)
                        pila.push_back({false, t});
            }
            for (const Abierto& a : pila)
                if (a.bucle)
                    simpleControlFlow = false;  // the ifs still open close at the end of the program; loops cannot
        }
        if (simpleControlFlow)
            for (auto [t1, t2] : elses)
            {
                elsesEn.insert(t1);
                ++ifEndLabels[t2];
            }
        else
            elsesEn.clear();
    }
    else if (!saltosIncondicionales.empty())
        simpleControlFlow = false;
#endif
    if (simpleControlFlow)
    {
        out += '\n';
        indentation = 1;
    }
    else
    {
#ifdef XENOS_GLSL
        out += "\n\tint pc = 0;\n";  // GLSL: the case labels are int
#else
        out += "\n\tuint pc = 0;\n";
#endif
        out += "\twhile (true)\n";
        out += "\t{\n";
        out += "\t\tswitch (pc)\n";
        out += "\t\t{\n";
    }

    controlFlowCode = code;
    instrAddress = 0;
    uint32_t pc = 0;

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : cfu.controlFlow)
        {
            if (!simpleControlFlow)
            {
                indentation = 3;
                println("\t\tcase {}:", pc);
            }
            else
            {
                auto findResult = ifEndLabels.find(pc);
                if (findResult != ifEndLabels.end())
                {
#ifdef HALO3_RECOMP
                    // At an if/else's T1 the outermost block closing here is that if: it becomes `} else {`.
                    const bool conElse = elsesEn.count(pc) != 0;
                    if (conElse)
                        cerrarPredicado();
#endif
                    for (uint32_t i = 0; i < findResult->second; i++)
                    {
#ifdef HALO3_RECOMP
                        // Never main's own brace: a label whose block was not opened in this form closes nothing.
                        if (indentation <= 1)
                            break;
#endif
                        --indentation;
                        indent();
                        out += "}\n";
#ifdef HALO3_RECOMP
                        if (conElse && i + 1 == findResult->second)
                        {
                            indent();
                            out += "else\n";
                            indent();
                            out += "{\n";
                            ++indentation;
                            marcaCierre_ = finCierre_ = std::string::npos;  // that `}` is not reopened
                            predGarantizado_ = -1;
                        }
#endif
                    }
                }
            }

            ++pc;

            uint32_t address = 0;
            uint32_t count = 0;
            uint32_t sequence = 0;
            bool shouldReturn = false;
            bool shouldCloseCurlyBracket = false;
            /*
             * If this CF opens its own `if (p0)` (CondExecPred), its condition. It serves
             * two purposes: reusing the identical block that was just closed, and leaving its
             * own brace recorded so the next one can reuse it. See reabrirCierre().
             */
            int condicionPredicadoCf = -1;

            // Control flow boundary. An open predicated block does not cross this point.
            cerrarPredicado();

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                count = cfInstr.exec.count;
                sequence = cfInstr.exec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::ExecEnd);
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                count = cfInstr.condExec.count;
                sequence = cfInstr.condExec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecEnd || cfInstr.opcode == ControlFlowOpcode::CondExecPredCleanEnd);
                {
                    // The condition belongs to the whole EXEC block, including its
                    // return. Per-instruction ALU predication does not replace it.
                    const auto booleano = boolConstants.find(cfInstr.condExec.boolAddress);
#ifdef HALO3_RECOMP
                    // Halo 3 uses the full 0-255 boolean register space, far past NFS's 16 bits per stage.
                    // halo_bool(A) (halo3_shader_common.h) reads bit A of a 256-bit boolean block, so no named
                    // boolean constant is needed.
                    indent();
                    if (booleano != boolConstants.end())
                        println("if ((g_Booleans & {}) {}= 0)", booleano->second, cfInstr.condExec.condition ? "!" : "=");
                    else
                        println("if (halo_bool({}) {}= 0)", uint32_t(cfInstr.condExec.boolAddress), cfInstr.condExec.condition ? "!" : "=");
#else
                    if (booleano == boolConstants.end())
                        throw std::runtime_error("EXEC condicional sin constante booleana declarada");
                    indent();
                    println("if ((g_Booleans & {}) {}= 0)", booleano->second, cfInstr.condExec.condition ? "!" : "=");
#endif
                    indent();
                    out += "{\n";
                    ++indentation;
                    shouldCloseCurlyBracket = true;
                }
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                count = cfInstr.condExecPred.count;
                sequence = cfInstr.condExecPred.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecPredEnd);
                condicionPredicadoCf = cfInstr.condExecPred.condition ? 1 : 0;
                /*
                 * The previous block was this same `if (p0)` and nothing was emitted between
                 * the two: its brace is deleted and this EXEC continues inside. This way runs of
                 * CondExecPred with the same predicate (the nine PCF taps, the six car lights)
                 * end up in one basic block instead of three or four.
                 */
                if (!reabrirCierre(condicionPredicadoCf))
                {
                    indent();
                    println("if ({}p0)", condicionPredicadoCf ? "" : "!");
                    indent();
                    out += "{\n";
                    ++indentation;
                }
                shouldCloseCurlyBracket = true;
                // This `if` governs the whole block, so the predicated instructions inside
                // do not have to test it again. See predGarantizado_.
                predGarantizado_ = condicionPredicadoCf;
                break;

            case ControlFlowOpcode::LoopStart:
                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    print("[unroll] ");
                #endif
                    println("for (aL = 0; aL < i{}.x; aL++)", uint32_t(cfInstr.loopStart.loopId));
                    indent();
                    out += "{\n";
                    ++indentation;
                }
                else 
                {
                    out += "\t\t\taL = 0;\n";
                }
                break;

            case ControlFlowOpcode::LoopEnd:
                if (simpleControlFlow)
                {
#ifdef HALO3_RECOMP
                    if (indentation > 1)  // never main's own brace
#endif
                    {
                        --indentation;
                        indent();
                        out += "}\n";
                    }
                }
                else
                {
                    out += "\t\t\t++aL;\n";
                    println("\t\t\tif (aL < i{}.x)", uint32_t(cfInstr.loopEnd.loopId));
                    out += "\t\t\t{\n";
                    println("\t\t\t\tpc = {};", uint32_t(cfInstr.loopEnd.address));
                    out += "\t\t\t\tcontinue;\n";
                    out += "\t\t\t}\n";
                }
                break;

            case ControlFlowOpcode::CondJmp:
            {
                if (cfInstr.condJmp.isUnconditional)
                {
#ifdef HALO3_RECOMP
                    if (simpleControlFlow)
                        break;  // an if/else's jump over the else part: the braces already do it
#endif
                    assert(!simpleControlFlow);
                    println("\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                    out += "\t\t\tcontinue;\n";
                }
                else
                {
                    indent();
                    if (cfInstr.condJmp.isPredicated)
                    {
                        println("if ({}p0)", cfInstr.condJmp.condition ^ simpleControlFlow ? "" : "!");
                    }
                    else
                    {
                        auto findResult = boolConstants.find(cfInstr.condJmp.boolAddress);
                        if (findResult != boolConstants.end())
                            println("if ((g_Booleans & {}) {}= 0)", findResult->second, cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
#ifdef HALO3_RECOMP
                        else
                            println("if (halo_bool({}) {}= 0)", uint32_t(cfInstr.condJmp.boolAddress), cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
#else
                        else
                            println("if (b{} {}= 0)", uint32_t(cfInstr.condJmp.boolAddress), cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
#endif
                    }

                    if (simpleControlFlow)
                    {
                        indent();
                        out += "{\n";
                        ++indentation;
                    }
                    else
                    {
                        out += "\t\t\t{\n";
                        println("\t\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                        out += "\t\t\t\tcontinue;\n";
                        out += "\t\t\t}\n";
                    }
                }
                break;
            }
            }

            auto instructionCode = code + address * 3;
            
            for (uint32_t i = 0; i < count; i++)
            {
                /*
                 * Named, for the same reason as the other two: GCC does not accept
                 * the anonymous struct inside a local union.
                 */
                union InstrWords
                {
                    VertexFetchInstruction vertexFetch;
                    TextureFetchInstruction textureFetch;
                    AluInstruction alu;
                    struct { uint32_t code0, code1, code2; } w;
                } iu;
                VertexFetchInstruction& vertexFetch = iu.vertexFetch;
                TextureFetchInstruction& textureFetch = iu.textureFetch;
                AluInstruction& alu = iu.alu;

                iu.w.code0 = instructionCode[0];
                iu.w.code1 = instructionCode[1];
                iu.w.code2 = instructionCode[2];
            
                if ((sequence & 0x1) != 0)
                {
                    if (vertexFetch.opcode == FetchOpcode::VertexFetch)
                    {
                        recompile(vertexFetch, address + i);
                    }
                    else
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (textureFetch.constIndex == 10) // g_GISampler
                        {
                            specConstantsMask |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                            indent();
                            out += "if (g_SpecConstants() & SPEC_CONSTANT_BICUBIC_GI_FILTER)";
                            indent();
                            out += '{';

                            ++indentation;
                            recompile(textureFetch, true);
                            --indentation;

                            indent();
                            out += "}";
                            indent();
                            out += "else";
                            indent();
                            out += '{';

                            ++indentation;
                            recompile(textureFetch, false);
                            --indentation;

                            indent();
                            out += '}';
                        }
                        else
                    #endif
                        {
                            recompile(textureFetch, false);
                        }
                    }
                }
                else
                {
                    recompile(alu);
                }
            
                sequence >>= 2;
                instructionCode += 3;
            }

            cerrarPredicado();  // end of the EXEC instructions

            if (shouldReturn)
            {
                if (isPixelShader)
                {
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TEST;

                    indent();
#ifdef XENOS_GLSL
                    out += "if ((g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST) != 0u)";  // no attributes, a bool
#else
                    out += "[branch] if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)";
#endif
                    indent();
                    out += '{';

                    indent();
                #ifdef NFSMW_RECOMP
                    // NFSMW: the 8 comparison functions (alphaTestValue, shader_common.h).
                    out += "\tclip(alphaTestValue(oC0.w));\n";
                #else
                    out += "\tclip(oC0.w - g_AlphaThreshold);\n";
                #endif

                    indent();
                    out += "}";

                #ifdef UNLEASHED_RECOMP
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;

                    indent();
                    out += "else if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TO_COVERAGE)";
                    indent();
                    out += '{';

                    indent();
                    out += "\toC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;\n";
                    indent();
                    out += "\toC0.w = 0.5 + (oC0.w - g_AlphaThreshold) / max(fwidth(oC0.w), 1e-6);\n";

                    indent();
                    out += '}';
                #endif
                }
                else
                {
                #ifdef UNLEASHED_RECOMP
                    if (!hasMtxProjection)
                #endif
                    {
                    #ifdef NFSMW_RECOMP
                        // To host clip space (ndc_scale/ndc_offset from
                        // draw.cpp): draws in pixels with clipping disabled.
                        out += "\toPos.xy = oPos.xy * g_NdcScale + g_NdcOffset * oPos.w;\n";
                    #endif
                    #ifdef HALO3_RECOMP
                        // OpenGL clip space (PA_CL_CLIP_CNTL dx_clip_space_def = 0, which Halo 3 sets): z runs from
                        // -w to w, the host clips at 0..w. The renderer puts 1.0 at shared byte 492 for those
                        // draws, and z becomes (z + w) / 2, as Xenia remaps it; 0.0 leaves D3D draws alone.
                        out += "\toPos.z = lerp(oPos.z, (oPos.z + oPos.w) * 0.5, NFSMW_COMPARTIDA_FLOAT(492));\n";
                    #endif
                        out += "\toPos.xy += g_HalfPixelOffset * oPos.w;\n";
                    }
                }

                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    if (hasMtxProjection)
                    {
                        out += "continue;\n";
                    }
                    else
                #endif
                    {
                        out += "return;\n";
                    }
                }
                else
                {
                    out += "\t\t\tbreak;\n";
                }
            }

#ifdef HALO3_RECOMP
            // Never main's own brace (an all-zero program reaches here with no block open).
            if (shouldCloseCurlyBracket && indentation > 1)
#else
            if (shouldCloseCurlyBracket)
#endif
            {
                --indentation;
                const size_t marcaLlaveCf = out.size();
                indent();
                out += "}\n";
                /*
                 * It is recorded in case the next CondExecPred is identical. Not with a
                 * `return` inside, since the block does not continue; and anotarCierreCf() also
                 * refuses if something wrote p0 in here (predGarantizado_ was set to -1): the
                 * next block has to test the new p0.
                 */
                anotarCierreCf(marcaLlaveCf, shouldReturn ? -1 : condicionPredicadoCf);
            }
            predGarantizado_ = -1;  // end of the EXEC block: it no longer guarantees anything
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

    if (!simpleControlFlow)
    {
        out += "\t\t\tbreak;\n";
        out += "\t\t}\n";
        out += "\t\tbreak;\n";
        out += "\t}\n";
    }
#ifdef HALO3_RECOMP
    else
    {
        // A forward jump to the end of the program (past the last control flow instruction) never reaches its label:
        // its block closes here, which is what jumping to the end means.
        cerrarPredicado();
        while (indentation > 1)
        {
            --indentation;
            indent();
            out += "}\n";
        }
    }
#endif

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
        out += "\t}\n";

    if (!isPixelShader && hasMtxProjection)
        out += "\toPos.xy += g_HalfPixelOffset * oPos.w;\n";
#endif

    out += "}";

#ifdef HALO3_RECOMP
    // The buffers the vertex fetches without an element read: storage buffers in set 5, bound in slot order from 0
    // (deko3d has 16 per stage). The slot is in the name, which is how the renderer knows what goes in each binding.
    if (!slotsVf.empty())
    {
        std::string declaraciones;
        uint32_t enlace = 0;
        for (uint32_t slot : slotsVf)
        {
            if (enlace == 16)
                declaraciones += "#error \"more than 16 vertex fetch buffers\"\n";
#ifdef XENOS_GLSL
            // What it reads per vertex (stride and offsets in dwords; stride 0 = mixed, the whole buffer), for the
            // renderer (rex/ui/deko3d/glsl.h, GlslDeTraductor).
            if (const auto alc = alcanceVf.find(slot); alc != alcanceVf.end())
                declaraciones += fmt::format("// xeno_vf {} {} {} {} {}\n", enlace, slot, alc->second.zancada,
                    alc->second.minimo, alc->second.maximo);
            // GLSL cannot pass a buffer block to a function: one accessor per slot, then the shared decoder.
            declaraciones += fmt::format("layout(std430, binding = {0}) readonly buffer XenoVf{1} {{ uint d[]; }} g_vf{1};\n"
                "float4 halo_vfetch_{1}(int dword, uint f, uint sg, uint en, int ex) {{ uint a = uint(max(dword, 0)); "
                "return halo_vf_decodificar(g_vf{1}.d[a], g_vf{1}.d[a + 1u], g_vf{1}.d[a + 2u], g_vf{1}.d[a + 3u], f, sg, en, "
                "ex); }}\n", enlace++, slot);
#else
            declaraciones += fmt::format("[[vk::binding({}, 5)]] ByteAddressBuffer g_vf{};\n", enlace++, slot);
#endif
        }
        out.insert(marcaBuffersVf, declaraciones + "\n");
    }
    // The samplers its texture fetches use: texture and sampler at binding = slot (0-31, no NFS shadow pair).
    // 1/size is in the shared constants for slots 0-15 only (360 + slot * 8); the rest get 0 (offsets ignored).
    if (!samplersHalo.empty())
    {
        std::string declaraciones = "#ifdef NFSMW_DEKO3D\n";
        for (auto [slot, dimension] : samplersHalo)
        {
            // A 1D fetch reads a 2D texture of height 1 (tfetch1D, halo3_shader_common.h).
            const char* nombre = dimension == uint32_t(TextureDimension::Texture3D) ? "3D"
                : dimension == uint32_t(TextureDimension::TextureCube) ? "Cube" : "2D";
#ifdef XENOS_GLSL
            // A combined sampler at binding = slot; the "sampler" argument of the helpers is a dummy 0.
            declaraciones += fmt::format("layout(binding = {0}) uniform sampler{1} s{0}_Textura;\n", slot, nombre);
            declaraciones += fmt::format("#define s{0}_Muestreo 0\n", slot);
#else
            declaraciones += fmt::format("[[vk::binding({0}, 0)]] Texture{1}<float4> s{0}_Textura;\n", slot, nombre);
            declaraciones += fmt::format("[[vk::binding({0}, 1)]] SamplerState s{0}_Muestreo;\n", slot);
#endif
            declaraciones += fmt::format("#define s{0}_Texture{1}DescriptorIndex s{0}_Textura\n", slot, nombre);
            if (strcmp(nombre, "2D") == 0)  // the same slot may also be fetched as 1D (a 2D texture of height 1)
                declaraciones += fmt::format("#define s{0}_Texture1DDescriptorIndex s{0}_Textura\n", slot);
            declaraciones += fmt::format("#define s{0}_SamplerDescriptorIndex s{0}_Muestreo\n", slot);
            if (slot < 16)
                declaraciones += fmt::format("#define s{0}_InvTamano float2(NFSMW_COMPARTIDA_FLOAT({1}), NFSMW_COMPARTIDA_FLOAT({2}))\n",
                    slot, 360 + slot * 8, 364 + slot * 8);
            else
                declaraciones += fmt::format("#define s{0}_InvTamano float2(0.0, 0.0)\n", slot);
        }
        declaraciones += "#endif\n\n";
        out.insert(marcaBuffersVf, declaraciones);
    }
#endif
}
