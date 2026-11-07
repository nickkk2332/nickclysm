from ..write_text import write_text


def parse_gunmod(json, origin):

    for mode in json.get("mode_modifier", []):
        write_text(mode[1], origin,
                   comment="Firing mode of gun mod")

    write_text(json.get("location"), origin,
               comment="Location of gun mod")

    for target in json.get("mod_targets", []):
        write_text(target, origin, context="gun_type_type",
                   comment="Target of gun mod")

    if "blacklist_message" in json:
        write_text(json["blacklist_message"], origin,
                   comment="Why a gun can't take gun mod \"{}\"".format(
                       json.get("id", "")))
