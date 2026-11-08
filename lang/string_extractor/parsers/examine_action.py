from ..write_text import write_text
from .effect import parse_effect_on_condition


def parse_process_message(json, origin, comment):
    if type(json) is list:
        for message in json:
            parse_process_message(message, origin, comment)
    elif type(json) is dict and "text" in json:
        write_text(json["text"], origin, comment=comment)
    else:
        write_text(json, origin, comment=comment)


def parse_examine_action(json, origin, name):
    if type(json) is list:
        for action in json:
            parse_examine_action(action, origin, name)
        return
    if not (json and type(json) is dict):
        return

    ename = f"examine action of {name}"

    write_text(json.get("name"), origin,
               comment=f"Menu entry of {ename}")
    write_text(json.get("query_msg"), origin,
               comment=f"Query message for {ename}")
    write_text(json.get("success_msg"), origin,
               comment=f"Message when {ename} succeeds")
    write_text(json.get("redundant_msg"), origin,
               comment=f"Message when {ename} would be redundant")
    write_text(json.get("condition_fail_msg"), origin,
               comment=f"Message when {ename} fails its condition")

    # process_start and process_finish
    for key in ["contains_message", "reject_message", "empty_message",
                "outside_message", "too_small_message", "no_energy_message",
                "ready_message", "success_message", "examine_message",
                "done_message"]:
        parse_process_message(json.get(key), origin,
                              f"Process message of {ename}")
    write_text(json.get("query"), origin,
               comment=f"Query of {ename}")
    time_left = json.get("time_left_message")
    if type(time_left) is dict:
        write_text(time_left.get("hours"), origin, plural=True,
                   comment=f"Hours left in {ename}")
        write_text(time_left.get("less_than_an_hour"), origin,
                   comment=f"Less than an hour left in {ename}")
        write_text(time_left.get("minutes"), origin, plural=True,
                   comment=f"Minutes left in {ename}")
    else:
        write_text(time_left, origin, comment=f"Time left in {ename}")

    for eoc in json.get("effect_on_conditions", []):
        parse_effect_on_condition(eoc, origin, ename)
    for eoc in json.get("hack_eocs", []):
        parse_effect_on_condition(eoc, origin, f"hack_eocs of {ename}")
