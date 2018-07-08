#!/usr/bin/python3
import sys
import os
import json

from collections import OrderedDict

FS_PATH = os.path.join(os.path.realpath(os.path.dirname(__file__)), 'fs')

if len(sys.argv) < 2:
    print("usage: %s <config file>" % sys.argv[0])
    exit(1)

conf_fname = sys.argv[1]
if not os.path.exists(conf_fname):
    print("Config file \"%s\" not found" % conf_fname)
    exit(2)

with open(conf_fname, 'r') as conf_file:
    if conf_fname.split('.')[-1] in ('yaml', 'yml'):
        import yaml

        conf_data = yaml.safe_load(conf_file.read())
    else:
        conf_data = json.load(conf_file, object_pairs_hook=OrderedDict)

if False:
    # Export print the JSON config as YAML
    import yaml
    import yamlordereddictloader

    print(yaml.dump(conf_data, indent=4, default_flow_style=False, Dumper=yamlordereddictloader.Dumper))

sensor_conf = conf_data.pop('sensors', [])
with open(os.path.join(FS_PATH, 'sensors.json'), 'w') as f:
    json.dump(sensor_conf, f, indent=4)
act_conf = conf_data.pop('actuators', [])
with open(os.path.join(FS_PATH, 'actuators.json'), 'w') as f:
    json.dump(act_conf, f, indent=4)

with open(os.path.join(FS_PATH, 'conf1.json'), 'w') as f:
    json.dump(conf_data, f, indent=4)
