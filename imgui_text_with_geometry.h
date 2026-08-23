#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <stdint.h>


struct ImTextGlyphGeometryData
{
public:
    uint32_t m_Unicode = UINT32_MAX;
    uint32_t m_StartVertexIndex = UINT32_MAX;
    bool bIsNewLine = false;

public:
    inline bool IsEmpty() const noexcept
    {
        return m_StartVertexIndex == UINT32_MAX;
    }
};

struct ImMulitlineTextGeometryData
{
public:
    ImVector<ImTextGlyphGeometryData> m_TextGlyphs;
    ImVector<ImVec2> m_LinesStartPos;
    ImDrawList* m_DrawList = nullptr;
    uint32_t m_FirstVertex = 0;
    uint32_t m_VertexCount = 0;

    ImVec2 m_TextCursorScreenPosition = {};
    ImVec2 m_TextCursorScreenRectMin = {};
    ImVec2 m_TextCursorScreenRectMax = {};
    uint32_t m_TextCursorLineIndex = 0;
    bool m_bHasValidCursorData = false;

    uint64_t m_BufferStartDisplay = 0;
public:
    void Clear()
    {
        m_DrawList = nullptr;
        m_FirstVertex = 0;
        m_VertexCount = 0;
        m_TextGlyphs.clear();
        m_LinesStartPos.clear();

        m_TextCursorLineIndex = 0;
        m_TextCursorScreenPosition = ImVec2();
        m_TextCursorScreenRectMin = {};
        m_TextCursorScreenRectMax = {};
        m_bHasValidCursorData = false;

        m_BufferStartDisplay = 0;
    }
};

namespace ImGui
{
	IMGUI_API bool          InputTextMultilineWithGeometry(const char* label, char* buf, size_t buf_size, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL, ImMulitlineTextGeometryData* textGeometryData = nullptr);
}
