#pragma once

#include "Glacier/ZMath.h"
#include "Glacier/ZString.h"

enum class TextAlignment {
    Left,
    Center,
    Right,
    Top,
    Middle,
    Bottom
};

enum class PrimitiveType {
    Line,
    Triangle,
    Text2D,
    Text3D
};

struct Line {
    SVector3 start;
    SVector3 end;
    SVector4 startColor;
    SVector4 endColor;
};

struct Triangle {
    SVector3 vertexPosition1;
    SVector3 vertexPosition2;
    SVector3 vertexPosition3;
    SVector4 vertexColor1;
    SVector4 vertexColor2;
    SVector4 vertexColor3;
    SVector2 textureCoordinates1;
    SVector2 textureCoordinates2;
    SVector2 textureCoordinates3;
};

struct Quad {
    SVector3 vertexPosition1;
    SVector3 vertexPosition2;
    SVector3 vertexPosition3;
    SVector3 vertexPosition4;
    SVector4 vertexColor1;
    SVector4 vertexColor2;
    SVector4 vertexColor3;
    SVector4 vertexColor4;
};

class IRenderer {
public:
    virtual void DrawLine3D(
        const SVector3& p_From, const SVector3& p_To, const SVector4& p_FromColor, const SVector4& p_ToColor
    ) = 0;

    virtual void DrawText2D(
        const ZString& p_Text, const SVector2& p_Pos, const SVector4& p_Color, float p_Rotation = 0.f,
        float p_Scale = 1.f, TextAlignment p_Alignment = TextAlignment::Center
    ) = 0;

    virtual bool WorldToScreen(const SVector3& p_WorldPos, SVector2& p_Out) = 0;
    virtual bool ScreenToWorld(const SVector2& p_ScreenPos, SVector3& p_WorldPosOut, SVector3& p_DirectionOut) = 0;
    virtual void DrawBox3D(const SVector3& p_Min, const SVector3& p_Max, const SVector4& p_Color) = 0;

    virtual void DrawOBB3D(
        const SVector3& p_Min, const SVector3& p_Max, const SMatrix& p_Transform, const SVector4& p_Color
    ) = 0;

    virtual void DrawBoundingQuads(
        const SVector3& p_Min, const SVector3& p_Max, const SMatrix& p_Transform, const SVector4& p_Color
    ) = 0;

    virtual void DrawTriangle3D(
        const SVector3& p_V1, const SVector4& p_Color1, const SVector3& p_V2, const SVector4& p_Color2, const SVector3& p_V3, const SVector4& p_Color3
    ) = 0;
    virtual void DrawTriangle3D(
        const SVector3& p_V1, const SVector4& p_Color1, const SVector2& p_TextureCoordinates1, const SVector3& p_V2, const SVector4& p_Color2,
        const SVector2& p_TextureCoordinates2, const SVector3& p_V3, const SVector4& p_Color3, const SVector2& p_TextureCoordinates3
    ) = 0;

    virtual void DrawQuad3D(
        const SVector3& p_V1, const SVector4& p_Color1, const SVector3& p_V2, const SVector4& p_Color2,
        const SVector3& p_V3, const SVector4& p_Color3, const SVector3& p_V4, const SVector4& p_Color4
    ) = 0;

    virtual void DrawText3D(
        const std::string& text, const SMatrix& world, const SVector4& color, float scale = 1.f,
        TextAlignment horizontalAlignment = TextAlignment::Left, TextAlignment verticalAlignment = TextAlignment::Top
    ) = 0;
    virtual void DrawText3D(
        const char* text, const SMatrix& world, const SVector4& color, float scale = 1.f,
        TextAlignment horizontalAlignment = TextAlignment::Left, TextAlignment verticalAlignment = TextAlignment::Top
    ) = 0;
    
    virtual void DrawMesh(const std::vector<SVector3>& vertices, const std::vector<unsigned short>& indices, const SVector4& vertexColor) = 0;

    virtual const PrimitiveType GetCurrentPrimitiveType() const = 0;
};
