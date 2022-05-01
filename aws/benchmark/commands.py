from os import stat
from os.path import join

from .settings import Settings
from .config import BenchParameters
from .utils import PathMaker

class CommandMaker:
    @staticmethod
    def run_primary(settings, id):
        assert isinstance(settings, Settings)
        assert isinstance(id, int)
        assert id >= 0
        cmd = [
            f'cd {PathMaker.project_dir(settings)}',
            f'./examples/hotstuff-app --conf hotstuff-sec{id}.conf 2>&1',
        ]
        return ' && '.join(cmd)

    @staticmethod
    def run_client(settings) -> str:
        assert isinstance(settings, Settings)
        root_dir = PathMaker.project_dir(settings)
        cmd = [
            f'cd {root_dir}',
            f'./examples/hotstuff-client --idx 0 --iter 1 2>&1'
        ]
        return ' && '.join(cmd)

    @staticmethod
    def cleanup(settings):
        assert isinstance(settings, Settings)
        root_dir = PathMaker.project_dir(settings)
        cmd = [
            f'rm -rf ~/pvss-sec* ~/pvss-setup.dat',

            # Delete any config files in the project folder
            f'cd {root_dir}',
            f'rm -rf pvss-sec* pvss-setup.dat hotstuff-sec* hotstuff.conf',
        ]
        return (' && '.join(cmd))

    @staticmethod
    def clean_logs(settings):
        assert isinstance(settings, Settings)
        root_dir = PathMaker.project_dir(settings)
        cmd = [
            f'rm -rf log*',
            f'cd {root_dir}',
            f'rm -rf log*'
        ]
        return ' && '.join(cmd)

    @staticmethod
    def compile(settings):
        assert isinstance(settings, Settings)
        root_dir = PathMaker.project_dir(settings)
        cmd = [
            f'cd {root_dir}',
            'cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=OFF ',
            'make'
        ]
        # TODO: Turn off logging
        return ' && '.join(cmd)

    @staticmethod
    def generate_key(settings, bench_parameters):
        assert isinstance(settings, Settings)
        assert isinstance(bench_parameters, BenchParameters)
        iter = bench_parameters.iter
        prefix = bench_parameters.prefix
        cmd = [
            f'cd {PathMaker.project_dir(settings)}',
            f'python scripts/gen_conf.py --iter {iter} --prefix {prefix} --ips ip.txt'
        ]
        return ' && '.join(cmd)

    # @staticmethod
    # def run_primary(keys, committee, store, parameters, debug=False):
    #     assert isinstance(keys, str)
    #     assert isinstance(committee, str)
    #     assert isinstance(parameters, str)
    #     assert isinstance(debug, bool)
    #     v = '-vvv' if debug else '-vv'
    #     return (f'./node {v} run --keys {keys} --committee {committee} '
    #             f'--store {store} --parameters {parameters} primary')

    # @staticmethod
    # def run_worker(keys, committee, store, parameters, id, debug=False):
    #     assert isinstance(keys, str)
    #     assert isinstance(committee, str)
    #     assert isinstance(parameters, str)
    #     assert isinstance(debug, bool)
    #     v = '-vvv' if debug else '-vv'
    #     return (f'./node {v} run --keys {keys} --committee {committee} '
    #             f'--store {store} --parameters {parameters} worker --id {id}')

    # @staticmethod
    # def run_client(address, size, rate, nodes):
    #     assert isinstance(address, str)
    #     assert isinstance(size, int) and size > 0
    #     assert isinstance(rate, int) and rate >= 0
    #     assert isinstance(nodes, list)
    #     assert all(isinstance(x, str) for x in nodes)
    #     nodes = f'--nodes {" ".join(nodes)}' if nodes else ''
    #     return f'./benchmark_client {address} --size {size} --rate {rate} {nodes}'

    @staticmethod
    def kill():
        cmd = [
            'tmux kill-server',
            'killall -9 hotstuff-app',
            'killall -9 hotstuff-client',
        ]
        return ' ; '.join(cmd)

    @staticmethod
    def generate_pvss(settings, nodes):
        assert isinstance(settings, Settings)
        assert isinstance(nodes, int)
        assert nodes > 0
        cmd = [
            f'cd {PathMaker.project_dir(settings)}',
            f'./pvss-setup --num {nodes}'
        ]
        return ' && '.join(cmd)