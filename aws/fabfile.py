from typing import OrderedDict
from fabric import task

from benchmark.utils import BenchError, Print
from benchmark.remote import Bench
from benchmark.instance import Manager

@task
def create(ctx, nodes=1):
    ''' Create a testbed '''
    try:
        Manager.make().create_instances(nodes)
    except BenchError as e:
        Print.error(e)

@task
def status(ctx):
    ''' Print a summary information about instances '''
    try:
        Manager.make().print_info()
    except BenchError as e:
        Print.error(e)

@task
def destroy(ctx):
    ''' Destroy the testbed '''
    try:
        Manager.make().terminate_instances()
    except BenchError as e:
        Print.error(e)

@task
def install(ctx):
    ''' Install the dependencies and download the codebase on all machines '''
    try:
        bencher = Bench(ctx)
        bencher.install()
    except BenchError as e:
        Print.error(e)

@task
def start(ctx, max=2):
    '''Start all the instances'''
    try:
        Manager.make().start_instances(max)
    except BenchError as e:
        Print.error(e)

@task
def shutdown(ctx):
    '''Shut down all the instances'''
    try:
        Manager.make().stop_instances()
    except BenchError as e:
        Print.error(e)

@task
def remote_optrand(ctx, nodes=17, debug=False):
    ''' Run benchmarks on AWS '''
    bench_params = {
        'nodes': [nodes],           # Number of replicas
        'workers': 1,               # Number of clients
        'iter': 1,                  # Number of iterations (Ask Nibesh)
        'prefix': 'hotstuff',       # Prefix for creating config
        'duration': 120,            # Time to run the experiment
        'runs': 1,                  # Num of times to run the experiment
    }
    try:
        Bench(ctx).run_bench_optrand(bench_params, debug)
    except BenchError as e:
        Print.error(e)
