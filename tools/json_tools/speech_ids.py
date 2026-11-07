#!/usr/bin/env python3
"""Give every "speech" line without an "id" one, so later JSON can change it.

The id is speech_<first speaker>_<n>: n counts up from 1 for the lines whose
first speaker is the same, in file order, after the highest n any of the given
files already uses for that speaker.  Ids already there never change, so
running this again only names new lines.  Run tools/format/json_formatter on
the files afterwards.

    tools/json_tools/speech_ids.py data/json/speech.json
"""

import argparse
import json
import re


def first_speaker(line):
    speaker = line.get("speaker")
    if isinstance(speaker, list):
        return speaker[0] if speaker else ""
    return speaker or ""


def speech_lines(data):
    objects = data if isinstance(data, list) else [data]
    return [o for o in objects
            if isinstance(o, dict) and o.get("type") == "speech"]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n", maxsplit=1)[0])
    parser.add_argument("files", nargs="+",
                        help="JSON files with speech lines")
    args = parser.parse_args()

    contents = {}
    for path in args.files:
        with open(path, encoding="utf-8") as f:
            contents[path] = json.load(f)

    used = {}
    taken = set()
    for data in contents.values():
        for line in speech_lines(data):
            if "id" not in line:
                continue
            taken.add(line["id"])
            speaker = first_speaker(line)
            match = re.fullmatch(re.escape("speech_" + speaker) + r"_(\d+)",
                                 line["id"])
            if match:
                used[speaker] = max(used.get(speaker, 0), int(match.group(1)))

    for path, data in contents.items():
        named = 0
        for line in speech_lines(data):
            if "id" in line or "abstract" in line:
                continue
            speaker = first_speaker(line)
            if not speaker:
                raise SystemExit(f"{path}: a speech line without a speaker")
            used[speaker] = used.get(speaker, 0) + 1
            new_id = f"speech_{speaker}_{used[speaker]}"
            if new_id in taken:
                raise SystemExit(f"{path}: {new_id} is already taken")
            taken.add(new_id)
            # "id" right after "type", the order the other members keep
            items = list(line.items())
            line.clear()
            for key, value in items:
                line[key] = value
                if key == "type":
                    line["id"] = new_id
            named += 1
        if named:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
                f.write("\n")
        print(f"{path}: {named} lines named")


if __name__ == "__main__":
    main()
