#pragma once

struct EdgeDetectionSettings
{
    float TexelSizeX = 1.0f / 1920.0f;
    float TexelSizeY = 1.0f / 1080.0f;
    float EdgeThreshold = 0.15f;
    float Padding = 0.0f;
};