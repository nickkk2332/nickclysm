from ..write_text import write_text


def parse_overmap_terrain(json, origin):
    write_text(json.get("name"), origin, comment="Overmap terrain name")
    radio = json.get("radio", [])
    if type(radio) is dict:
        radio = [radio]
    for station in radio:
        if "message" in station:
            write_text(station["message"], origin,
                       comment="Radio broadcast of overmap terrain \"{}\"".format(
                           json.get("id")))
