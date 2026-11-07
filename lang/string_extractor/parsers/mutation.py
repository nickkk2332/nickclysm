from .effect import parse_effect_on_condition
from .enchant import parse_enchant
from ..helper import get_singular_name
from ..write_text import write_text


def parse_mutation(json, origin):

    if json.get("debug") or json.get("dummy"):
        return

    name = get_singular_name(json)

    is_visible_to_player = (json.get("player_display", True) or
                            json.get("starting_trait") or
                            json.get("initial_ma_styles") or
                            json.get("//I18N"))

    if is_visible_to_player:
        write_text(json.get("name"), origin, comment="Mutation name")
        write_text(json.get("description"), origin,
                   comment=f"Description of mutation '{name}'")

    attacks = json.get("attacks", [])
    if type(attacks) is dict:
        attacks = [attacks]

    for attack in attacks:
        write_text(attack.get("attack_text_u"), origin,
                   comment="Message when player with mutation"
                   f" '{name}' attacks")
        write_text(attack.get("attack_text_npc"), origin,
                   comment="Message when NPC with mutation"
                   f" '{name}' attacks")

    write_text(json.get("unwearable_message"), origin,
               comment="Message when a character with mutation"
               f" '{name}' can't wear an item")

    if "ranged_mutation" in json:
        write_text(json["ranged_mutation"].get("message"), origin,
                   comment="Message when firing ranged attack"
                   f" with mutation '{name}'")

    if "message" in json.get("spawn_item", []):
        spawn_type = json["spawn_item"]["type"]
        write_text(json["spawn_item"]["message"], origin,
                   comment="Message when spawning item"
                   f" '{spawn_type}' with mutation '{name}'")

    for arr in json.get("triggers", []):
        for trigger in arr:
            if "msg_on" in trigger:
                write_text(trigger["msg_on"].get("text"), origin,
                           comment=f"Trigger message of mutation '{name}'")
            if "msg_off" in trigger:
                write_text(trigger["msg_off"].get("text"), origin,
                           comment=f"Trigger message of mutation '{name}'")

    for variant in json.get("variants", []):
        variant_name = get_singular_name(variant)
        write_text(variant["name"], origin,
                   comment=f"Variant name of mutation '{name}'")
        write_text(variant["description"], origin,
                   comment="Description of variant"
                   f" '{variant_name}' of mutation '{name}'")

    if "transform" in json:
        target_id = json["transform"]["target"]
        write_text(json["transform"]["msg_transform"], origin,
                   comment="Message when transforming from mutation"
                   f" '{name}' to '{target_id}'")

    for enchantment in json.get("enchantments", []):
        parse_enchant(enchantment, origin)

    if "activation_msg" in json:
        write_text(json["activation_msg"], origin,
                   comment=f"Activation message of mutation '{name}'")

    if "activation_fail_message" in json:
        write_text(json["activation_fail_message"], origin,
                   comment=f"Message when mutation '{name}' fails its "
                   "activation condition")

    eat_conditions = json.get("eat_condition", [])
    if type(eat_conditions) is dict:
        eat_conditions = [eat_conditions]
    for eat_condition in eat_conditions:
        if "fail_message" in eat_condition:
            write_text(eat_condition["fail_message"], origin,
                       comment=f"Message when mutation '{name}' refuses "
                       "food")

    for eoc in json.get("eat_eocs", []):
        parse_effect_on_condition(eoc, origin)
