from .effect import parse_effect_on_condition


def parse_damage_rule(json, origin):
    for field in ("mutate", "on_resolved"):
        for eoc in json.get(field, []):
            parse_effect_on_condition(eoc, origin)
