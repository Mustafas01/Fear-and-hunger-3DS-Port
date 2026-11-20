
import json
import struct
import sys
from pathlib import Path


MAGIC = b"FHEC"
VERSION = 1


def get_int(obj, *keys, default=0):
    for key in keys:
        if key in obj and obj[key] is not None:
            return int(obj[key])
    return default


def encode_json(value):
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":")
    ).encode("utf-8")


def compile_events(input_path, output_path):
    input_path = Path(input_path)
    output_path = Path(output_path)

    if not input_path.exists():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    with input_path.open("r", encoding="utf-8-sig") as f:
        data = json.load(f)

    # Support the exporter's wrapper format.
    if "FearAndHunger3DS_EventCommands" in data:
        data = data["FearAndHunger3DS_EventCommands"]

    map_id = get_int(data, "mapId")
    events = data.get("events", [])

    if not isinstance(events, list):
        raise ValueError("'events' must be a list")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("wb") as out:

        # File header:
        # magic[4], version[u16], map_id[u16], event_count[u32]
        out.write(struct.pack(
            "<4sHHI",
            MAGIC,
            VERSION,
            map_id,
            len(events)
        ))

        total_pages = 0
        total_commands = 0

        for event in events:
            event_id = get_int(event, "eventId", "id")
            x = get_int(event, "x")
            y = get_int(event, "y")
            pages = event.get("pages", [])

            if not isinstance(pages, list):
                raise ValueError(
                    f"Event {event_id}: pages must be a list"
                )

            if len(pages) > 65535:
                raise ValueError(
                    f"Event {event_id}: too many pages"
                )

            # Event header:
            # event_id[u16], x[u16], y[u16], page_count[u16]
            out.write(struct.pack(
                "<HHHH",
                event_id,
                x,
                y,
                len(pages)
            ))

            for page_number, page in enumerate(pages):
                page_index = get_int(
                    page,
                    "pageIndex",
                    default=page_number
                )

                # Exporter may call this "list" or "commands".
                commands = page.get(
                    "list",
                    page.get("commands", [])
                )

                if not isinstance(commands, list):
                    raise ValueError(
                        f"Event {event_id}, page {page_index}: "
                        "commands must be a list"
                    )

                conditions = page.get("conditions", {})
                if not isinstance(conditions, dict):
                    conditions = {}

                conditions_bytes = encode_json(conditions)

                # Page header:
                # page_index[u16], command_count[u16],
                # conditions_length[u32]
                out.write(struct.pack(
                    "<HHI",
                    page_index,
                    len(commands),
                    len(conditions_bytes)
                ))

                out.write(conditions_bytes)

                total_pages += 1

                for command_number, command in enumerate(commands):
                    if not isinstance(command, dict):
                        raise ValueError(
                            f"Event {event_id}, page {page_index}, "
                            f"command {command_number}: invalid command"
                        )

                    code = get_int(command, "code")
                    indent = get_int(command, "indent")
                    parameters = command.get("parameters", [])

                    if not isinstance(parameters, list):
                        raise ValueError(
                            f"Event {event_id}, page {page_index}, "
                            f"command {command_number}: "
                            "parameters must be a list"
                        )

                    params_bytes = encode_json(parameters)

                    if len(params_bytes) > 0xFFFFFFFF:
                        raise ValueError("Command parameters too large")

                    # Command header:
                    # code[u16], indent[i16], params_length[u32]
                    out.write(struct.pack(
                        "<HhI",
                        code,
                        indent,
                        len(params_bytes)
                    ))

                    out.write(params_bytes)

                    total_commands += 1

    print("EVENT COMPILATION COMPLETE")
    print(f"Map ID:       {map_id}")
    print(f"Events:       {len(events)}")
    print(f"Pages:        {total_pages}")
    print(f"Commands:     {total_commands}")
    print(f"Output:       {output_path}")
    print(f"File size:    {output_path.stat().st_size:,} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(
            "Usage:\n"
            "python compile_events.py <input.json> <output.bin>"
        )
        sys.exit(1)

    try:
        compile_events(sys.argv[1], sys.argv[2])
    except Exception as error:
        print(f"ERROR: {error}")
        sys.exit(1)
