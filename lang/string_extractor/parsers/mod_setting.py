from ..helper import get_singular_name
from ..write_text import write_text


def parse_mod_setting(json, origin):
    name = get_singular_name(json["name"]) if "name" in json else json["id"]

    if "name" in json:
        write_text(json["name"], origin, comment="Name of a mod setting")
    if "description" in json:
        write_text(json["description"], origin,
                   comment=f"Description of the mod setting '{name}'")
    if "group" in json:
        write_text(json["group"], origin,
                   comment="Group of mod settings in the mod settings "
                   f"menu, from the mod setting '{name}'")
    for choice in json.get("choices", []):
        write_text(choice.get("name"), origin,
                   comment=f"A choice of the mod setting '{name}'")
