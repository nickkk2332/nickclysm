from ..write_text import write_text
from .effect import parse_effect_on_condition


def parse_execution(json, origin):
    name = json.get("id", "execution")
    write_text(json.get("name"), origin,
               comment=f"Name of execution '{name}'")

    def eocs(entries):
        for entry in entries:
            parse_effect_on_condition(entry, origin)

    for field in ("on_start", "on_tick", "on_complete",
                  "on_interrupt", "on_end"):
        eocs(json.get(field, []))

    for step, definition in json.get("steps", {}).items():
        action = definition.get("action", {})
        comment = f"Narration in execution '{name}', step '{step}'"
        write_text(action.get("text"), origin, comment=comment)
        write_text(action.get("audible_text"), origin, comment=comment)
        for entry in action.get("pool", {}).get("entries", []):
            write_text(entry.get("text"), origin, comment=comment)
        narration = action.get("narration", {}).get("on_moved", {})
        write_text(narration.get("text"), origin, comment=comment)
        write_text(narration.get("audible_text"), origin, comment=comment)
        eocs(action.get("eocs", []))
