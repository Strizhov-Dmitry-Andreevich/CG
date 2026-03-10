Texture2D gCurrentFrame : register(t0);
Texture2D gHistory : register(t1);
Texture2D gVelocity : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

cbuffer TAAConstants : register(b0)
{
    float blendFactor;
    float3 padding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

struct PSOut
{
    float4 BackBuffer : SV_Target0;
    float4 HistoryTexture : SV_Target1;
};

VSOut VS(uint vid : SV_VertexID)
{
    VSOut output;
    
    // Координаты вершин полноэкранного треугольника
    float2 positions[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1)
    };
    
    output.PosH = float4(positions[vid], 0, 1);
    output.TexC = output.PosH.xy * 0.5 + 0.5;
    output.TexC.y = 1.0 - output.TexC.y;
    
    return output;
}


PSOut PS(VSOut pin) : SV_Target

{
    PSOut pout;
    
    // Читаем velocity
    float2 velocity = gVelocity.Sample(gsamPointClamp, pin.TexC).xy;
    float motion = length(velocity);
    
    // Репроекция: ищем в истории где был этот пиксель
    float2 prevTexC = pin.TexC - velocity;
    float4 historyColor = gHistory.Sample(gsamLinearClamp, prevTexC);
    
    // Текущий цвет (с джиттером)
    float4 currentColor = gCurrentFrame.Sample(gsamPointClamp, pin.TexC);

    // Соседние пиксели для построения AABB
    float4 NearColor0 = gCurrentFrame.Sample(gsamLinearWrap, pin.TexC, int2(1, 0));
    float4 NearColor1 = gCurrentFrame.Sample(gsamLinearWrap, pin.TexC, int2(0, 1));
    float4 NearColor2 = gCurrentFrame.Sample(gsamLinearWrap, pin.TexC, int2(-1, 0));
    float4 NearColor3 = gCurrentFrame.Sample(gsamLinearWrap, pin.TexC, int2(0, -1));
    
    // Строим AABB
    float4 BoxMin = min(currentColor, min(NearColor0, min(NearColor1, min(NearColor2, NearColor3))));
    float4 BoxMax = max(currentColor, max(NearColor0, max(NearColor1, max(NearColor2, NearColor3))));
    
    // Clamping истории
    historyColor = clamp(historyColor, BoxMin, BoxMax);
    
    // Адаптивный blend factor на основе движения
    float motionFactor = saturate(motion * 100.0f);
    float adaptiveBlend = lerp(blendFactor, 1.0f, motionFactor);
    
    // Финальный результат
    pout.BackBuffer = lerp(historyColor, currentColor, adaptiveBlend);
    //pout.BackBuffer = gVelocity.Sample(gsamPointClamp, pin.TexC).rgbr * 10.0f;
    pout.HistoryTexture = pout.BackBuffer; // Обновляем историю
    
    // Вариант 1: показать только текущий кадр
    //pout.BackBuffer = currentColor;
    //pout.HistoryTexture = currentColor;

    // Вариант 2: показать velocity
    //pout.BackBuffer = float4(velocity*1000.f, 0.0f, 1.0f);
    //pout.HistoryTexture = pout.BackBuffer;
    

    // Вариант 3: показать историю
    //pout.BackBuffer = historyColor;
    //pout.HistoryTexture = historyColor;
    
        // Показываем разницу между current и history
    //float diff = length(currentColor.rgb - historyColor.rgb);
   //pout.BackBuffer = float4(diff, diff, diff, 1.0f);
    //pout.HistoryTexture = currentColor; // Пишем current в историю
    
    return pout;
}
