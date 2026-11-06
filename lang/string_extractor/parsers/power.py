from ..write_text import write_text
from .effect import parse_effect_on_condition


def parse_power(json, origin):
    write_text(json.get("name"), origin,
               comment=f"Name of {json.get('type', 'power')} '{json.get('id', '')}'")
    for field in ("on_start", "on_tick", "on_end"):
        for eoc in json.get(field, []):
            parse_effect_on_condition(eoc, origin)
