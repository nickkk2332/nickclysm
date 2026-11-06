from ..write_text import write_text
from .effect import parse_effect_on_condition


def parse_ability(json, origin):
    write_text(json.get("name"), origin,
               comment=f"Name of ability '{json.get('id', '')}'")
    for field in ("eocs", "on_start", "before_release", "on_complete",
                  "on_interrupt", "on_end"):
        for eoc in json.get(field, []):
            parse_effect_on_condition(eoc, origin)
