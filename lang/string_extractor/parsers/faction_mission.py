from ..helper import get_singular_name
from ..write_text import write_text


def parse_faction_mission(json, origin):
    name = get_singular_name(json)

    write_text(json.get("name"), origin, comment="Faction mission name")

    write_text(json.get("desc"), origin,
               comment=f"Description of faction mission '{name}'")

    write_text(json.get("time"), origin,
               comment=f"Duration of faction mission '{name}'")

    write_text(json.get("footer"), origin,
               comment=f"Footer of faction mission '{name}'")

    write_text(json.get("items_label"), origin,
               comment=f"Gather items header of faction mission '{name}'")

    for item in json.get("items_possibilities", []):
        write_text(item, origin,
                   comment=f"Possible item from faction mission '{name}'")

    for item in json.get("effects", []):
        write_text(item, origin,
                   comment=f"Effect of faction mission '{name}'")

    write_text(json.get("action_name"), origin,
               comment=f"Menu entry that sends a companion on faction mission "
               f"'{name}'")

    write_text(json.get("return_name"), origin,
               comment=f"Menu entry that brings a companion back from faction "
               f"mission '{name}'")

    write_text(json.get("progress"), origin,
               comment=f"Header of the companions out on faction mission "
               f"'{name}'")

    write_text(json.get("depart_message"), origin,
               comment=f"Message after a companion's name when it sets off on "
               f"faction mission '{name}'")

    write_text(json.get("return_message"), origin,
               comment=f"Message after a companion's name when it comes back "
               f"from faction mission '{name}'")

    if "encounter" in json:
        write_text(json["encounter"].get("task"), origin,
                   comment=f"What a companion on faction mission '{name}' was "
                   f"doing, as in 'While %s, a silent specter approaches'")
