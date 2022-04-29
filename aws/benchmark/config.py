from json import dump, load
from collections import OrderedDict

class ConfigError(Exception):
    pass

class BenchParameters:
    def __init__(self, json):
        try:
            nodes = json['nodes']
            nodes = nodes if isinstance(nodes, list) else [nodes]
            if not nodes or any(x <= 1 for x in nodes):
                raise ConfigError('Missing or invalid number of nodes')
            self.nodes = [int(x) for x in nodes]

            self.iter = int(json['iter']) if 'iter' in json else 1
            self.prefix = json['prefix'] if 'prefix' in json else "hotstuff"
            self.workers = int(json['workers'])
            self.duration = int(json['duration'])
            self.runs = int(json['runs']) if 'runs' in json else 1
        except KeyError as e:
            raise ConfigError(f'Malformed bench parameters: missing key {e}')

        except ValueError:
            raise ConfigError('Invalid parameters type')

