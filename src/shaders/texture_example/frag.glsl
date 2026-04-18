uniform sampler2D tex;

void main() {
    vec2 uv = uvBottomLeft();
    FragColor = texture(tex, uv);
}
