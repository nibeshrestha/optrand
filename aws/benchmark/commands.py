from os import stat
from os.path import join

from .config import BenchParameters
from .utils import PathMaker

class CommandMaker:
    @staticmethod
    def client_log_file(repo_name):
        assert isinstance(repo_name, str)
        return join(repo_name, f'log-client')

    @staticmethod
    def log_file(repo_name, id):
        assert isinstance(repo_name, str)
        assert isinstance(id, int)
        assert id >= 0
        return join(repo_name, f'log{id}')

    @staticmethod
    def run_primary(repo_name, id):
        assert isinstance(repo_name, str)
        assert isinstance(id, int)
        assert id >= 0
        return f'(cd {repo_name}; ./examples/hotstuff-app --conf hotstuff-sec{id}.conf &> log{id})'

    @staticmethod
    def run_client(repo_name):
        assert isinstance(repo_name, str)
        return f'(cd {repo_name}; ./examples/hotstuff-client --idx 0 --iter 1 &> log-client )'


    @staticmethod
    def cleanup():
        return (
            f'rm -r .db-* ; rm .*.json ; mkdir -p {PathMaker.results_path()}'
        )

    @staticmethod
    def clean_logs():
        return f'rm -r {PathMaker.logs_path()} ; mkdir -p {PathMaker.logs_path()}'

    @staticmethod
    def compile():
        return 'cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=OFF && make'

    @staticmethod
    def generate_key(bench_parameters):
        assert isinstance(bench_parameters, BenchParameters)
        iter = bench_parameters.iter
        prefix = bench_parameters.prefix
        return f'python scripts/gen_conf.py --iter {iter} --prefix {prefix} --ips ip.txt'

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
        return 'tmux kill-server'

    @staticmethod
    def generate_pvss(bench_parameters):
        assert isinstance(bench_parameters, BenchParameters)
        nodes = max(bench_parameters.nodes)
        return f'./pvss-setup --num {nodes}'

    @staticmethod
    def alias_binaries(origin):
        assert isinstance(origin, str)
        node = join(origin, 'examples/hotstuff-app')
        client = join(origin, 'examples/hotstuff-client')
        pvss_setup = join(origin, 'pvss-setup')
        return f'rm hotstuff-app ; rm hotstuff-client ; rm pvss-setup ; ln -s {node} . ; ln -s {client} . ; ln -s {pvss_setup}'