# Copyright(C) Facebook, Inc. and its affiliates.
from os import stat
from os.path import join

from .settings import Settings

class BenchError(Exception):
    def __init__(self, message, error):
        assert isinstance(error, Exception)
        self.message = message
        self.cause = error
        super().__init__(message)

class PathMaker:
    @staticmethod
    def project_dir(settings):
        assert isinstance(settings, Settings)
        return f'{settings.repo_name}'

    @staticmethod
    def client_log_file(settings):
        assert isinstance(settings, Settings)
        return join(PathMaker.project_dir(settings), f'log-client')

    @staticmethod
    def log_file(settings, id):
        assert isinstance(settings, Settings)
        assert isinstance(id, int)
        assert id >= 0
        return join(PathMaker.project_dir(settings), f'log{id}')

    @staticmethod
    def hotstuff_config(settings, id, local=False):
        assert isinstance(settings, Settings)
        assert isinstance(id, int)
        assert id >= 0
        if local:
            return f"hotstuff-sec{id}.conf"
        else:
            root_dir = PathMaker.project_dir(settings)
            return join(root_dir, f'hotstuff-sec{id}.conf')

    @staticmethod
    def pvss_config(settings, id, local=False):
        assert isinstance(settings, Settings)
        assert isinstance(id, int)
        assert id >= 0
        if local:
            return f'pvss-sec{id}.conf'
        else:
            root_dir = PathMaker.project_dir(settings)
            return join(root_dir, f'pvss-sec{id}.conf')

    @staticmethod
    def hotstuff_setup_file(settings, local=False):
        assert isinstance(settings, Settings)
        if local:
            return 'hotstuff.conf'
        else:
            root_dir = PathMaker.project_dir(settings)
            return join(root_dir, "hotstuff.conf")

    @staticmethod
    def pvss_setup_file(settings, local=False):
        assert isinstance(settings, Settings)
        if local:
            return 'pvss-setup.dat'
        else:
            root_dir = PathMaker.project_dir(settings)
            return join(root_dir, "pvss-setup.dat")

    @staticmethod
    def ip_file(settings, local=False):
        assert isinstance(settings, Settings)
        if local:
            return 'ip.txt'
        else:
            root_dir = PathMaker.project_dir(settings)
            return join(root_dir, 'ip.txt')


class Color:
    HEADER = '\033[95m'
    OK_BLUE = '\033[94m'
    OK_GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    END = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class Print:
    @staticmethod
    def heading(message):
        assert isinstance(message, str)
        print(f'{Color.OK_GREEN}{message}{Color.END}')

    @staticmethod
    def info(message):
        assert isinstance(message, str)
        print(message)

    @staticmethod
    def warn(message):
        assert isinstance(message, str)
        print(f'{Color.BOLD}{Color.WARNING}WARN{Color.END}: {message}')

    @staticmethod
    def error(e):
        assert isinstance(e, BenchError)
        print(f'\n{Color.BOLD}{Color.FAIL}ERROR{Color.END}: {e}\n')
        causes, current_cause = [], e.cause
        while isinstance(current_cause, BenchError):
            causes += [f'  {len(causes)}: {e.cause}\n']
            current_cause = current_cause.cause
        causes += [f'  {len(causes)}: {type(current_cause)}\n']
        causes += [f'  {len(causes)}: {current_cause}\n']
        print(f'Caused by: \n{"".join(causes)}\n')


def progress_bar(iterable, prefix='', suffix='', decimals=1, length=30, fill='█', print_end='\r'):
    total = len(iterable)

    def printProgressBar(iteration):
        formatter = '{0:.'+str(decimals)+'f}'
        percent = formatter.format(100 * (iteration / float(total)))
        filledLength = int(length * iteration // total)
        bar = fill * filledLength + '-' * (length - filledLength)
        print(f'\r{prefix} |{bar}| {percent}% {suffix}', end=print_end)

    printProgressBar(0)
    for i, item in enumerate(iterable):
        yield item
        printProgressBar(i + 1)
    print()
