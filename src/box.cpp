#include <fstream>

#include "box.hpp"

extern std::ofstream file;

auto Box::component_resize() -> void {
	if (child != 0) {
		auto child_width  = internal.width  - 2;
		auto child_height = internal.height - 2;
		if (child_width > internal.width) {
			child_width = 0;
		}
		if (child_height > internal.height) {
			child_height = 0;
		}
		child->window_resize(internal.x + 1, internal.y + 1, child_width, child_height);
	}
}

Box::Box(const std::string & name) :
	Window(),
	name(name),
	child(0) {}

Box::Box(Window & root, const std::string & name) :
	Window(root, true),
	name(name),
	child(0) {}

Box::Box(Window & root, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::string & name) :
	Window(root, x, y, width, height),
	name(name),
	child(0) {}

auto Box::set_child(Window * child) -> void {
	this->child = child;
	component_resize();
}

auto Box::set_name(const std::string & name) -> void {
	this->name = name;
}

auto Box::draw() -> Window& {
	if (internal.width > 0 && internal.height > 0) {
		if (internal.height == 1) {
			move(0, 0).horz_line(internal.width, ACS_HLINE);
		} else if (internal.width == 1) {
			move(0, 0).vert_line(internal.height, ACS_VLINE);
		} else {
			move(0,                  0                  ).add_char(ACS_ULCORNER);
			move(internal.width - 1, 0                  ).add_char(ACS_URCORNER);
			move(0,                  internal.height - 1).add_char(ACS_LLCORNER);
			move(internal.width - 1, internal.height - 1).add_char(ACS_LRCORNER);
			move(1,                  0                  ).horz_line(internal.width  - 2, ACS_HLINE);
			move(1,                  internal.height - 1).horz_line(internal.width  - 2, ACS_HLINE);
			move(0,                  1                  ).vert_line(internal.height - 2, ACS_VLINE);
			move(internal.width - 1, 1                  ).vert_line(internal.height - 2, ACS_VLINE);
		}
		if (name.size() > 0) {
			if (name.size() + 6 <= internal.width && internal.height > 1) {
				move(2, 0).print(" %s ", name.c_str());
			} else if (name.size() + 4 <= internal.width) {
				move(1, 0).print(" %s ", name.c_str());
			} else if (internal.width >= 7) {
				move(1, 0).print(" %s... ", name.substr(0, internal.width - 7).c_str());
			}
		}
	}
	if (child != 0) {
		child->draw();
	}
	return *this;
}

auto Box::clear() -> Window& {
	Window::clear();
	if (child != 0) {
		child->clear();
	}
	return *this;
}

auto Box::refresh() -> Window& {
	Window::refresh();
	if (child != 0) {
		child->refresh();
	}
	return *this;
}

Box::~Box() {}
