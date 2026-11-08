from ..write_text import write_text


def parse_ammunition_type(json, origin):
    write_text(json.get("name"), origin, comment="Name of an ammunition type")
    ammo_id = json.get("id")
    fire_sound = json.get("fire_sound", {})
    write_text(fire_sound.get("message"), origin,
               comment=f"Sound of firing a gun of ammunition type '{ammo_id}'")
    write_text(fire_sound.get("burst_message"), origin,
               comment="Sound of a burst from a gun of ammunition type "
               f"'{ammo_id}'")
    write_text(json.get("pyromania_message"), origin,
               comment="Message when firing satisfies a pyromaniac; "
               "%s is the gun's name")
