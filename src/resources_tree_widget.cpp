#include "resources_tree_widget.hpp"
#include "gui_window.hpp"
#include "gui.hpp"
#include "color.hpp"
#include "debug.hpp"
#include "spritesheet.hpp"
#include "settings_file.hpp"
#include "scroll_bar_widget.hpp"

static void device_change_callback(Event, void *userdata) {
    ResourcesTreeWidget *resources_tree = (ResourcesTreeWidget *)userdata;
    resources_tree->refresh_devices();
    resources_tree->update_model();
}

ResourcesTreeWidget::ResourcesTreeWidget(GuiWindow *gui_window, SettingsFile *settings_file) :
    Widget(gui_window),
    context(gui_window->gui->_genesis_context),
    gui(gui_window->gui),
    text_color(color_fg_text()),
    padding_top(4),
    padding_bottom(0),
    padding_left(4),
    padding_right(0),
    icon_spacing(4),
    icon_width(12),
    icon_height(12),
    item_padding_top(4),
    item_padding_bottom(4),
    settings_file(settings_file)
{
    display_node_count = 0;

    scroll_bar = create<ScrollBarWidget>(gui_window, ScrollBarLayoutVert);
    dummy_label = create<Label>(gui);

    gui->events.attach_handler(EventAudioDeviceChange, device_change_callback, this);
    gui->events.attach_handler(EventMidiDeviceChange, device_change_callback, this);

    root_node = create_parent_node(nullptr, "");
    root_node->indent_level = -1;
    root_node->parent_data->expanded = true;

    playback_devices_root = create_parent_node(root_node, "Playback Devices");
    recording_devices_root = create_parent_node(root_node, "Recording Devices");
    midi_devices_root = create_parent_node(root_node, "MIDI Devices");
    samples_root = create_parent_node(root_node, "Samples");

    refresh_devices();
    scan_sample_dirs();
}

ResourcesTreeWidget::~ResourcesTreeWidget() {
    clear_display_nodes();

    gui->events.detach_handler(EventAudioDeviceChange, device_change_callback);
    gui->events.detach_handler(EventMidiDeviceChange, device_change_callback);

    destroy_node(root_node);

    destroy(dummy_label, 1);
    destroy(scroll_bar, 1);
}

void ResourcesTreeWidget::clear_display_nodes() {
    for (int i = 0; i < display_nodes.length(); i += 1) {
        NodeDisplay *node_display = display_nodes.at(i);
        destroy_node_display(node_display);
    }
    display_nodes.clear();
}

void ResourcesTreeWidget::draw(const glm::mat4 &projection) {
    bg.draw(gui_window, projection);
    scroll_bar->draw(projection);

    for (int i = 0; i < display_node_count; i += 1) {
        NodeDisplay *node_display = display_nodes.at(i);
        node_display->label->draw(projection * node_display->label_model, text_color);
        if (should_draw_icon(node_display->node)) {
            gui->draw_image_color(gui_window, node_display->node->icon_img,
                    projection * node_display->icon_model, text_color);
        }
    }
}

void ResourcesTreeWidget::refresh_devices() {
    int audio_device_count = genesis_get_audio_device_count(context);
    int midi_device_count = genesis_get_midi_device_count(context);
    int default_playback_index = genesis_get_default_playback_device_index(context);
    int default_recording_index = genesis_get_default_recording_device_index(context);
    int default_midi_index = genesis_get_default_midi_device_index(context);

    int record_i = 0;
    int playback_i = 0;
    for (int i = 0; i < audio_device_count; i += 1) {
        GenesisAudioDevice *audio_device = genesis_get_audio_device(context, i);
        bool playback = (genesis_audio_device_purpose(audio_device) == GenesisAudioDevicePurposePlayback);
        Node *node;
        if (playback) {
            if (playback_i < playback_devices_root->parent_data->children.length()) {
                node = playback_devices_root->parent_data->children.at(playback_i);
            } else {
                node = create_playback_node();
            }
        } else {
            if (record_i < recording_devices_root->parent_data->children.length()) {
                node = recording_devices_root->parent_data->children.at(record_i);
            } else {
                node = create_record_node();
            }
        }
        genesis_audio_device_unref(node->audio_device);
        node->audio_device = audio_device;
        String text = genesis_audio_device_description(audio_device);
        if (playback && i == default_playback_index) {
            text.append(" (default)");
        } else if (!playback && i == default_recording_index) {
            text.append(" (default)");
        }
        node->text = text;

        if (playback) {
            playback_i += 1;
        } else {
            record_i += 1;
        }
    }
    while (record_i < recording_devices_root->parent_data->children.length()) {
        pop_destroy_child(recording_devices_root);
    }
    while (playback_i < playback_devices_root->parent_data->children.length()) {
        pop_destroy_child(playback_devices_root);
    }

    int i;
    for (i = 0; i < midi_device_count; i += 1) {
        GenesisMidiDevice *midi_device = genesis_get_midi_device(context, i);
        Node *node;
        if (i < midi_devices_root->parent_data->children.length()) {
            node = midi_devices_root->parent_data->children.at(i);
            genesis_midi_device_unref(node->midi_device);
        } else {
            node = create_midi_node();
        }
        node->midi_device = midi_device;
        String text = genesis_midi_device_description(midi_device);
        if (i == default_midi_index)
            text.append(" (default)");
        node->text = text;
    }
    while (i < midi_devices_root->parent_data->children.length()) {
        pop_destroy_child(midi_devices_root);
    }
}

ResourcesTreeWidget::NodeDisplay * ResourcesTreeWidget::create_node_display(Node *node) {
    NodeDisplay *result = create<NodeDisplay>();
    result->label = create<Label>(gui);
    result->node = node;
    node->display = result;
    ok_or_panic(display_nodes.append(result));
    return result;
}

void ResourcesTreeWidget::destroy_node_display(NodeDisplay *node_display) {
    if (node_display) {
        if (node_display->node)
            node_display->node->display = nullptr;
        destroy(node_display->label, 1);
        destroy(node_display, 1);
    }
}

void ResourcesTreeWidget::update_model() {

    int available_width = width - scroll_bar->width;
    int available_height = height - padding_bottom - padding_top;

    bg.update(this, 0, 0, available_width, height);

    // compute item positions
    int next_top = padding_top;
    update_model_stack.clear();
    ok_or_panic(update_model_stack.append(root_node));
    while (update_model_stack.length() > 0) {
        Node *child = update_model_stack.pop();
        add_children_to_stack(update_model_stack, child);
        if (child->indent_level == -1)
            continue;
        child->top = next_top;
        next_top += item_padding_top + dummy_label->height() + item_padding_bottom;
        child->bottom = next_top;
    }

    int full_height = next_top;

    scroll_bar->left = left + width - scroll_bar->min_width();
    scroll_bar->top = top;
    scroll_bar->width = scroll_bar->min_width();
    scroll_bar->height = height;
    scroll_bar->min_value = 0;
    scroll_bar->max_value = max(0, full_height - available_height);
    scroll_bar->set_handle_ratio(available_height, full_height);
    scroll_bar->set_value(scroll_bar->value);
    scroll_bar->on_resize();

    // now consider scroll position and create display nodes for nodes that
    // are visible
    display_node_count = 0;
    update_model_stack.clear();
    ok_or_panic(update_model_stack.append(root_node));
    while (update_model_stack.length() > 0) {
        Node *child = update_model_stack.pop();
        add_children_to_stack(update_model_stack, child);
        if (child->indent_level == -1)
            continue;

        if (child->bottom - scroll_bar->value >= padding_top &&
            child->top - scroll_bar->value < padding_top + available_height)
        {
            NodeDisplay *node_display;
            if (display_node_count >= display_nodes.length())
                node_display = create_node_display(child);
            else
                node_display = display_nodes.at(display_node_count);
            display_node_count += 1;

            node_display->node = child;
            child->display = node_display;

            node_display->top = child->top - scroll_bar->value;
            node_display->bottom = child->bottom - scroll_bar->value;

            int extra_indent = (child->icon_img != nullptr);
            int label_left = padding_left + (icon_width + icon_spacing) *
                (child->indent_level + extra_indent);
            int label_top = node_display->top + item_padding_top;
            node_display->label_model = transform2d(label_left, label_top);
            node_display->label->set_text(child->text);
            node_display->label->update();

            if (child->icon_img) {
                node_display->icon_left = padding_left + (icon_width + icon_spacing) * child->indent_level;
                node_display->icon_top = node_display->top +
                    (node_display->bottom - node_display->top) / 2 - icon_height / 2;
                float icon_scale_width = icon_width / (float)child->icon_img->width;
                float icon_scale_height = icon_height / (float)child->icon_img->height;
                node_display->icon_model = transform2d(
                        node_display->icon_left, node_display->icon_top,
                        icon_scale_width, icon_scale_height);
            }
        }
    }
}

void ResourcesTreeWidget::add_children_to_stack(List<Node *> &stack, Node *node) {
    if (node->node_type != NodeTypeParent)
        return;
    if (!node->parent_data->expanded)
        return;

    for (int i = node->parent_data->children.length() - 1; i >= 0; i -= 1) {
        Node *child = node->parent_data->children.at(i);
        child->indent_level = node->indent_level + 1;
        ok_or_panic(stack.append(child));
    }
}

ResourcesTreeWidget::Node *ResourcesTreeWidget::create_playback_node() {
    Node *node = ok_mem(create_zero<Node>());
    node->node_type = NodeTypePlaybackDevice;
    node->parent_node = playback_devices_root;
    node->icon_img = gui->img_volume_up;
    ok_or_panic(node->parent_node->parent_data->children.append(node));
    return node;
}

ResourcesTreeWidget::Node *ResourcesTreeWidget::create_record_node() {
    Node *node = ok_mem(create_zero<Node>());
    node->node_type = NodeTypeRecordingDevice;
    node->parent_node = recording_devices_root;
    node->icon_img = gui->img_microphone;
    ok_or_panic(node->parent_node->parent_data->children.append(node));
    return node;
}

ResourcesTreeWidget::Node *ResourcesTreeWidget::create_midi_node() {
    Node *node = ok_mem(create_zero<Node>());
    node->node_type = NodeTypeMidiDevice;
    node->parent_node = midi_devices_root;
    ok_or_panic(node->parent_node->parent_data->children.append(node));
    return node;
}

ResourcesTreeWidget::Node *ResourcesTreeWidget::create_parent_node(Node *parent, const char *text) {
    Node *node = ok_mem(create_zero<Node>());
    node->text = text;
    node->icon_img = gui->img_plus;
    node->node_type = NodeTypeParent;
    node->parent_data = create<ParentNode>();
    node->parent_data->expanded = false;
    node->parent_node = parent;
    if (parent)
        ok_or_panic(parent->parent_data->children.append(node));
    return node;
}

ResourcesTreeWidget::Node *ResourcesTreeWidget::create_sample_file_node(Node *parent, OsDirEntry *dir_entry) {
    Node *node = ok_mem(create_zero<Node>());
    node->node_type = NodeTypeSampleFile;
    node->text = dir_entry->name;
    node->parent_node = parent;
    node->dir_entry = dir_entry;
    node->icon_img = gui->img_entry_file;
    ok_or_panic(node->parent_node->parent_data->children.append(node));
    return node;
}

void ResourcesTreeWidget::pop_destroy_child(Node *node) {
    Node *child = node->parent_data->children.pop();
    child->parent_node = nullptr;
    destroy_node(child);
}

void ResourcesTreeWidget::destroy_node(Node *node) {
    if (node) {
        destroy(node->parent_data, 1);
        genesis_audio_device_unref(node->audio_device);
        genesis_midi_device_unref(node->midi_device);
        os_dir_entry_unref(node->dir_entry);
        destroy(node, 1);
    }
}

void ResourcesTreeWidget::on_mouse_move(const MouseEvent *event) {
    if (event->action != MouseActionDown)
        return;

    for (int i = 0; i < display_nodes.length(); i += 1) {
        NodeDisplay *node_display = display_nodes.at(i);
        Node *node = node_display->node;
        if (node->node_type == NodeTypeParent) {
            if (event->x >= node_display->icon_left &&
                event->x < node_display->icon_left + icon_width + icon_spacing &&
                event->y >= node_display->top &&
                event->y < node_display->bottom)
            {
                toggle_expansion(node);
                break;
            }
        }
    }
}

void ResourcesTreeWidget::toggle_expansion(Node *node) {
    node->parent_data->expanded = !node->parent_data->expanded;
    node->icon_img = node->parent_data->expanded ? gui->img_minus : gui->img_plus;
    update_model();
}

bool ResourcesTreeWidget::should_draw_icon(Node *node) {
    if (!node->icon_img)
        return false;
    if (node->node_type == NodeTypeParent && node->parent_data->children.length() == 0)
        return false;
    return true;
}

void ResourcesTreeWidget::delete_all_children(Node *root) {
    assert(root->node_type == NodeTypeParent);
    List<Node *> pending;
    ok_or_panic(pending.append(root));
    while (pending.length() > 0) {
        Node *node = pending.pop();
        if (node->node_type == NodeTypeParent) {
            for (int i = 0; i < node->parent_data->children.length(); i += 1) {
                ok_or_panic(pending.append(node->parent_data->children.at(i)));
            }
        }
        if (node != root)
            destroy_node(node);
    }
}

static int compare_is_dir_then_name(OsDirEntry *a, OsDirEntry *b) {
    if (a->is_dir && !b->is_dir) {
        return -1;
    } else if (b->is_dir && !a->is_dir) {
        return 1;
    } else {
        return ByteBuffer::compare(a->name, b->name);
    }
}

void ResourcesTreeWidget::scan_dir_recursive(const ByteBuffer &dir, Node *parent_node) {
    List<OsDirEntry *> entries;
    int err = os_readdir(dir.raw(), entries);
    if (err)
        fprintf(stderr, "Error reading %s: %s\n", dir.raw(), genesis_error_string(err));
    entries.sort<compare_is_dir_then_name>();
    for (int i = 0; i < entries.length(); i += 1) {
        OsDirEntry *dir_entry = entries.at(i);
        if (dir_entry->is_dir) {
            Node *child = create_parent_node(parent_node, dir_entry->name.raw());
            ByteBuffer full_path = os_path_join(dir, dir_entry->name);
            scan_dir_recursive(full_path, child);
            os_dir_entry_unref(dir_entry);
        } else {
            create_sample_file_node(parent_node, dir_entry);
        }
    }
}

void ResourcesTreeWidget::scan_sample_dirs() {
    List<ByteBuffer> dirs;
    ok_or_panic(dirs.append(os_get_samples_dir()));
    for (int i = 0; i < settings_file->sample_dirs.length(); i += 1) {
        ok_or_panic(dirs.append(settings_file->sample_dirs.at(i)));
    }

    delete_all_children(samples_root);

    for (int i = 0; i < dirs.length(); i += 1) {
        Node *parent_node = create_parent_node(samples_root, dirs.at(i).raw());
        scan_dir_recursive(dirs.at(i), parent_node);
    }
}
