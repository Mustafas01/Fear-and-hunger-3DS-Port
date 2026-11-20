import os
import sys
import json
import struct


MAGIC = b"FHEV"
VERSION = 1

# Condition bits
COND_SWITCH1 = 1 << 0
COND_SWITCH2 = 1 << 1
COND_VARIABLE = 1 << 2
COND_SELF_SWITCH = 1 << 3
COND_ITEM = 1 << 4
COND_ACTOR = 1 << 5


def write_string(f, value):
    """Write a UTF-8 string with a uint16 length prefix."""
    data = (value or "").encode("utf-8")

    if len(data) > 65535:
        raise ValueError("String is too long for binary format")

    f.write(struct.pack("<H", len(data)))
    f.write(data)


def get_page_conditions(page):
    c = page.get("conditions", {})

    flags = 0

    if c.get("switch1Valid"):
        flags |= COND_SWITCH1

    if c.get("switch2Valid"):
        flags |= COND_SWITCH2

    if c.get("variableValid"):
        flags |= COND_VARIABLE

    if c.get("selfSwitchValid"):
        flags |= COND_SELF_SWITCH

    if c.get("itemValid"):
        flags |= COND_ITEM

    if c.get("actorValid"):
        flags |= COND_ACTOR

    self_switch = c.get("selfSwitchCh", "A")
    if not self_switch:
        self_switch = "A"

    self_switch = ord(str(self_switch)[0].upper())

    return (
        flags,
        int(c.get("switch1Id", 0)),
        int(c.get("switch2Id", 0)),
        int(c.get("variableId", 0)),
        int(c.get("variableValue", 0)),
        int(c.get("actorId", 0)),
        int(c.get("itemId", 0)),
        self_switch,
    )


def get_graphic(page):
    graphic = page.get("image", {})

    return (
        graphic.get("characterName", ""),
        int(graphic.get("characterIndex", 0)),
        int(graphic.get("direction", 2)),
        int(graphic.get("pattern", 1)),
        int(graphic.get("tileId", 0)),
    )


def validate_u16(value, name):
    if not 0 <= value <= 65535:
        raise ValueError(f"{name} is outside uint16 range: {value}")
    return value


def export_events(game_root, map_id, output_dir):
    data_dir = os.path.join(game_root, "www", "data")
    map_path = os.path.join(data_dir, f"Map{map_id:03d}.json")

    if not os.path.isfile(map_path):
        raise FileNotFoundError(map_path)

    os.makedirs(output_dir, exist_ok=True)

    with open(map_path, encoding="utf-8") as f:
        map_data = json.load(f)

    events = [
        event for event in map_data.get("events", [])
        if event is not None
    ]

    map_width = validate_u16(int(map_data["width"]), "map width")
    map_height = validate_u16(int(map_data["height"]), "map height")
    map_id = validate_u16(map_id, "map ID")
    event_count = validate_u16(len(events), "event count")

    binary_path = os.path.join(output_dir, "events.bin")
    commands_path = os.path.join(output_dir, "events_commands.json")

    # Binary header:
    # magic[4], version, map ID, width, height, event count
    with open(binary_path, "wb") as f:
        f.write(struct.pack(
            "<4sHHHHH",
            MAGIC,
            VERSION,
            map_id,
            map_width,
            map_height,
            event_count
        ))

        for event in events:
            event_id = validate_u16(int(event["id"]), "event ID")
            x = validate_u16(int(event["x"]), "event X")
            y = validate_u16(int(event["y"]), "event Y")

            pages = event.get("pages", [])
            page_count = validate_u16(len(pages), "page count")

            # Event header: ID, X, Y, page count
            f.write(struct.pack("<HHHH", event_id, x, y, page_count))

            for page in pages:
                conditions = get_page_conditions(page)

                (
                    condition_flags,
                    switch1_id,
                    switch2_id,
                    variable_id,
                    variable_value,
                    actor_id,
                    item_id,
                    self_switch
                ) = conditions

                (
                    character_name,
                    character_index,
                    direction,
                    pattern,
                    tile_id
                ) = get_graphic(page)

                trigger = int(page.get("trigger", 0))
                priority = int(page.get("priorityType", 1))
                through = int(bool(page.get("through", False)))

                # Page metadata:
                # flags, switch1, switch2, variable ID, variable value,
                # actor ID, item ID, self-switch character,
                # tile ID, character index, direction, pattern,
                # trigger, priority, through
                f.write(struct.pack(
                    "<HHHHiHHB HBBBBBB",
                    condition_flags,
                    switch1_id,
                    switch2_id,
                    variable_id,
                    variable_value,
                    actor_id,
                    item_id,
                    self_switch,
                    tile_id,
                    character_index,
                    direction,
                    pattern,
                    trigger,
                    priority,
                    through
                ))

                write_string(f, character_name)

    # Preserve the complete event command lists for the interpreter.
    command_data = {
        "format": "FearAndHunger3DS_EventCommands",
        "version": 1,
        "mapId": map_id,
        "events": []
    }

    for event in events:
        command_data["events"].append({
            "id": event["id"],
            "x": event["x"],
            "y": event["y"],
            "pages": [
                {
                    "pageIndex": page_index,
                    "list": page.get("list", [])
                }
                for page_index, page in enumerate(event.get("pages", []))
            ]
        })

    with open(commands_path, "w", encoding="utf-8") as f:
        json.dump(command_data, f, ensure_ascii=False, separators=(",", ":"))

    total_pages = sum(len(e.get("pages", [])) for e in events)

    print()
    print("EVENT EXPORT COMPLETE")
    print("---------------------")
    print(f"Map:             {map_id:03d}")
    print(f"Dimensions:      {map_width} x {map_height}")
    print(f"Events:          {event_count}")
    print(f"Pages:           {total_pages}")
    print(f"Binary:          {binary_path}")
    print(f"Commands:        {commands_path}")
    print()


def main():
    if len(sys.argv) != 4:
        print(
            'Usage: python export_events.py "<game root>" <map ID> "<output folder>"'
        )
        sys.exit(1)

    game_root = sys.argv[1]
    map_id = int(sys.argv[2])
    output_dir = sys.argv[3]

    export_events(game_root, map_id, output_dir)


if __name__ == "__main__":
    main()