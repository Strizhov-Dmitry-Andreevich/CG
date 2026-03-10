#include "Default.hlsl"

PixelOut WirePS(DS_OUTPUT pin) : SV_Target
{
    PixelOut pout;
    pout.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    pout.Velocity = float4(0.0f, 0.0f, 0.0f, 0.0f); // Нулевой velocity для wireframe
    return pout;
    //return float4(0.87f, 0.f, 0.87f, 1.f); //fioletoviy
    //return float4(0.24f, 0.67f, 0.24f, 1.0f);//сетка цвета влюбленной жабы
}