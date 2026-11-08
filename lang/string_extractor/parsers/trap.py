from ..helper import get_singular_name
from ..write_text import write_text
from .effect import parse_effect_on_condition


def parse_trap(json, origin):
    name = get_singular_name(json)

    write_text(json.get("name"), origin, comment="Name of a trap")

    if "vehicle_data" in json:
        write_text(json["vehicle_data"].get("sound"), origin,
                   comment=f"Trap-vehicle collision message for trap '{name}'")

    for key in ["memorial_male", "memorial_female"]:
        write_text(json.get(key), origin,
                   comment=f"Memorial message of trap '{name}'")

    write_text(json.get("trigger_message_u"), origin,
               comment=f"Message when player triggers trap '{name}'")

    write_text(json.get("trigger_message_npc"), origin,
               comment=f"Message when NPC triggers trap '{name}'")

    write_text(json.get("disarm_condition_msg"), origin,
               comment=f"Message when the disarm condition of trap '{name}'"
               " isn't met")

    for key in ["eocs", "item_eocs", "disarm_eocs", "disarm_fail_eocs"]:
        for eoc in json.get(key, []):
            parse_effect_on_condition(eoc, origin, f"{key} of trap '{name}'")
