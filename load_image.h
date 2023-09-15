#pragma once
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <string>
#include <stdexcept>

class stb_image {
    uint8_t* data_;
public:
    const size_t w, h, channels;
	stb_image(uint8_t*&& data, size_t w, size_t h, size_t c) : data_(data), w(w), h(h), channels(c) { data = nullptr; }
	stb_image(stb_image&& o) : stb_image(std::move(o.data_), o.w, o.h, o.channels) { o.data_ = nullptr; }
	~stb_image() { stbi_image_free(data_); }
	uint8_t* begin() { return data(); }
	uint8_t* end() { return data() + size(); }
	const uint8_t* begin() const { return data(); }
	const uint8_t* end() const { return data() + size(); }
	uint8_t* data() { return data_; }
	const uint8_t* data() const { return data_; }
	size_t size() const { return w*h*channels; }
};

inline stb_image load_image(std::string_view path) {
    int w, h, channels;
    stbi_uc* pixels = stbi_load(std::string(path).c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("failed to load image");
    }
    return stb_image(std::move(pixels), w, h, channels);
}
