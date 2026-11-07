from ..helper import get_singular_name
from ..write_text import write_text


def parse_fault(json, origin):
    name = get_singular_name(json)

    write_text(json["name"], origin,
               comment="Name of a fault")

    write_text(json["description"], origin,
               comment=f"Description of fault '{name}'")

    write_text(json.get("message"), origin,
               comment=f"Message of fault occur '{name}'")

    write_text(json.get("item_prefix"), origin,
               comment=f"Prefix for item's name '{name}'")

    write_text(json.get("item_suffix"), origin,
               comment=f"Suffix for item's name '{name}'")

    if "runover" in json:
        write_text(json["runover"].get("message"), origin,
                   comment="Message when running something over gives a "
                   f"vehicle wheel fault '{name}'")

    if "while_moving" in json:
        write_text(json["while_moving"].get("sound"), origin,
                   comment=f"Sound of a moving vehicle wheel with fault '{name}'")
        write_text(json["while_moving"].get("message"), origin,
                   comment="Message when driving damages a vehicle wheel "
                   f"with fault '{name}'")
