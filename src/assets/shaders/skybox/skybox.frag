#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform float timeOfDay; // 0.0 = midnight, 0.5 = noon, 1.0 = midnight

// Procedural sky colors based on time of day
vec3 GetSkyColor(vec3 direction, float time) {
    float sunHeight = sin(time * 3.14159 * 2.0 - 1.57079); // -1 to 1
    
    // Sky gradient colors
    vec3 dayTopColor = vec3(0.3, 0.5, 0.9);      // Blue sky top
    vec3 dayHorizonColor = vec3(0.7, 0.8, 0.95); // Light blue horizon
    vec3 sunsetColor = vec3(1.0, 0.5, 0.2);      // Orange sunset
    vec3 nightTopColor = vec3(0.01, 0.01, 0.05); // Dark night top
    vec3 nightHorizonColor = vec3(0.05, 0.05, 0.15); // Dark night horizon
    
    // Calculate gradient factor (0 = horizon, 1 = top)
    float heightFactor = max(direction.y, 0.0);
    
    // Interpolate between top and horizon colors
    vec3 dayColor = mix(dayHorizonColor, dayTopColor, heightFactor);
    vec3 nightColor = mix(nightHorizonColor, nightTopColor, heightFactor);
    
    // Determine if we're in day, night, or transition
    float dayNightBlend = smoothstep(-0.2, 0.2, sunHeight);
    vec3 skyColor = mix(nightColor, dayColor, dayNightBlend);
    
    // Add sunset/sunrise colors near horizon during transitions
    if (sunHeight < 0.3 && sunHeight > -0.3 && heightFactor < 0.3) {
        float sunsetBlend = (1.0 - abs(sunHeight) / 0.3) * (1.0 - heightFactor / 0.3);
        skyColor = mix(skyColor, sunsetColor, sunsetBlend * 0.8);
    }
    
    return skyColor;
}

// Simple sun rendering
vec3 RenderSun(vec3 direction, float time) {
    // Sun direction based on time
    float angle = time * 3.14159 * 2.0;
    vec3 sunDir = normalize(vec3(cos(angle) * 0.3, sin(angle), -0.5));
    
    // Calculate sun glow
    float sunDot = max(dot(direction, sunDir), 0.0);
    float sunGlow = pow(sunDot, 512.0); // Sharp sun disk
    float sunHalo = pow(sunDot, 8.0) * 0.3; // Soft glow around sun
    
    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    
    return (sunGlow + sunHalo) * sunColor;
}

void main()
{
    vec3 direction = normalize(TexCoords);
    
    // Get base sky color
    vec3 skyColor = GetSkyColor(direction, timeOfDay);
    
    // Add sun
    vec3 sunContribution = RenderSun(direction, timeOfDay);
    
    // Combine
    vec3 finalColor = skyColor + sunContribution;
    
    FragColor = vec4(finalColor, 1.0);
}
