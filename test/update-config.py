
from pathlib import Path
import configparser
r=Path(__file__).resolve().parent.parent
p=r/"groups.ini"
before=p.read_text(encoding="utf-8-sig")
(r/"test"/"build"/"groups.before-update.ini").write_text(before,encoding="utf-8")
cfg=configparser.ConfigParser(interpolation=None,strict=False,delimiters=("=",),comment_prefixes=(";","#"),empty_lines_in_values=False)
cfg.optionxform=str
cfg.read_string(before)
original={section:dict(cfg[section]) for section in ("Programs","Paths","Icons") if cfg.has_section(section)}
text=before.rstrip()+"\n"
# Append recognized sections so all prior grouping entries and comments remain byte-for-byte.
extra=[]
settings=cfg["Settings"] if cfg.has_section("Settings") else {}
missing=[]
if not any(k.lower()=="iconmatch" for k in settings): missing.append("IconMatch=path-class-id")
if not any(k.lower()=="defaultgrouplabelsize" for k in settings): missing.append("DefaultGroupLabelSize=12")
if missing:extra+=["[Settings]"]+missing+[""]
if not cfg.has_section("Paths"):extra+=["[Paths]","; Full path = group; all icons for that path.",""]
for name,comment in [("GroupLabelSizes","; Group = font size (8..32 points)."),("GroupLabelWidths","; Group = label width (120..600 logical pixels)."),("GroupLabelHeights","; Group = minimum height (32..160), 0 = automatic.")]:
    if not cfg.has_section(name):extra += ["["+name+"]",comment,""]
text+="\n"+"\n".join(extra)+"\n"
# Fold duplicate Settings headers into the original section for conventional INI tools.
lines=text.splitlines()
out=[];section=None;settings_seen=False;pending=[]
for line in lines:
    stripped=line.strip()
    if stripped.lower()=="[settings]":
        if settings_seen:section="extra-settings";continue
        settings_seen=True;section="settings"
    elif stripped.startswith("[") and stripped.endswith("]"):section=stripped[1:-1].lower()
    if section=="extra-settings":
        if stripped and not stripped.startswith((";","#")):pending.append(line)
    else:out.append(line)
if pending:
    i=next(i for i,l in enumerate(out) if l.strip().lower()=="[settings]")+1
    out[i:i]=pending
text="\n".join(out).rstrip()+"\n"
cfg2=configparser.ConfigParser(interpolation=None,strict=False,delimiters=("=",),comment_prefixes=(";","#"),empty_lines_in_values=False)
cfg2.optionxform=str;cfg2.read_string(text)
for section,values in original.items():assert dict(cfg2[section])==values,(section,"grouping changed")
temporary=p.with_name("groups.ini.tmp");temporary.write_text(text,encoding="utf-8");temporary.replace(p)
print("Local config updated; all existing grouping rules preserved")
