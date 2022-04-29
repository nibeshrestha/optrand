import subprocess
from time import sleep
from fabric import task, Connection, ThreadingGroup as Group, transfer
from fabric.exceptions import GroupException
from paramiko import PasswordRequiredException, RSAKey, SSHException
from os.path import splitext, basename

from .config import BenchParameters, ConfigError
from .commands import CommandMaker
from .utils import BenchError, PathMaker, Print, progress_bar
from .instance import Manager

class FabricError(Exception):
    ''' Wrapper for Fabric exception with a meaningfull error message. '''

    def __init__(self, error):
        assert isinstance(error, GroupException)
        message = list(error.result.values())[-1]
        super().__init__(message)

class ExecutionError(Exception):
    pass

class Bench:
    def __init__(self, ctx):
        self.manager = Manager.make()
        self.settings = self.manager.settings
        try:
            ctx.connect_kwargs.pkey = RSAKey.from_private_key_file(
                self.manager.settings.key_path
            )
            self.connect = ctx.connect_kwargs
        except (IOError, PasswordRequiredException, SSHException) as e:
            raise BenchError('Failed to load SSH key', e)

    def _check_stderr(self, output):
        if isinstance(output, dict):
            for x in output.values():
                if x.stderr:
                    raise ExecutionError(x.stderr)
        else:
            if output.stderr:
                raise ExecutionError(output.stderr)

    def install(self):
        Print.info('Installing dependencies and cloning the repo...')
        cmd = [
            # Clone the repo.
            f'(git clone {self.settings.repo_url} || (cd {self.settings.repo_name} ; git pull))',
        
            # Run the packages dependency installer
            f"(cd {self.settings.repo_name}; bash docker-setup.sh)",

            # Copy some libraries
            "sudo cp /usr/local/lib/libJerasure.* /usr/lib",
            "sudo cp /usr/local/lib/libgf_complete.* /usr/lib",
        ]
        try:
            num_nodes = self._run(cmd)
            Print.heading(f'Initialized testbed of {num_nodes} nodes')
        except (GroupException, ExecutionError) as e:
            e = FabricError(e) if isinstance(e, GroupException) else e
            raise BenchError('Failed to install repo on testbed', e)
    
    def _run(self, cmd) -> int: 
        hosts = self.manager.hosts(flat=True)
        g = Group(*hosts, user='ubuntu', connect_kwargs=self.connect)
        g.run(' && '.join(cmd), hide=True)
    
    def kill(self, hosts=[], delete_logs=False):
        assert isinstance(hosts, list)
        assert isinstance(delete_logs, bool)
        hosts = hosts if hosts else self.manager.hosts(flat=True)
        delete_logs = CommandMaker.clean_logs() if delete_logs else 'true'
        cmd = [delete_logs, f'({CommandMaker.kill()} || true)']
        try:
            g = Group(*hosts, user='ubuntu', connect_kwargs=self.connect)
            g.run(' && '.join(cmd), hide=True)
        except GroupException as e:
            raise BenchError('Failed to kill nodes', FabricError(e))

    def _select_hosts(self, bench_parameters):
        # Spawn the primary and each worker on a different machine. Each
        # authority runs in a single data center.
        nodes = max(bench_parameters.nodes)
        clients = bench_parameters.workers

        # Ensure there are enough hosts.
        hosts = self.manager.hosts()
        if sum(len(x) for x in hosts.values()) < nodes+clients:
            return []
        ordered = zip(*hosts.values())
        ordered = [x for y in ordered for x in y]
        return ordered[:nodes+clients]

    def _generate_ip_files(self, hosts, bench_parameters):
        assert isinstance(bench_parameters, BenchParameters)
        assert isinstance(hosts, list)
        nodes = max(bench_parameters.nodes)
        if len(hosts) < nodes:
            raise FabricError("Not enough hosts to create the IP file")
        with open("/tmp/ip.txt", "w+") as f:
            for ip in hosts[:nodes]:
                print(ip, file=f)
            print("", file=f)

    def _config(self, hosts, bench_parameters):
        assert isinstance(bench_parameters, BenchParameters)
        Print.info('Generating configuration files...')

        # DONE: Generate ip files
        Print.info('Generating IP files...')
        self._generate_ip_files(hosts, bench_parameters)

        assert isinstance(hosts, list)
        ip = hosts[0]
        c = Connection(ip, user='ubuntu', connect_kwargs=self.connect)
        # Cleanup all local configuration files.
        cmd = CommandMaker.cleanup()
        c.run(cmd, hide=True)

        # Recompile the latest code.
        cmd = CommandMaker.compile()
        c.run(f'cd {self.settings.repo_name} && {cmd}', hide=True)

        # Create alias for the client and nodes binary.
        cmd = CommandMaker.alias_binaries(self.settings.repo_name)
        c.run(cmd, hide=True)

        # DONE: Generate PVSS config files
        c.put("/tmp/ip.txt", f'{self.settings.repo_name}/ip.txt')
        cmd = CommandMaker.generate_pvss(bench_parameters)
        try: 
            c.run(cmd, hide=True)
        except Exception as e:
            Print.warn("Please fix this pvss config generation error {e}")

        # DONE: Generate hotstuff-config files
        cmd = CommandMaker.generate_key(bench_parameters)
        c.run(f'cd {self.settings.repo_name} && {cmd}', hide=True)

        # DONE: Download the setup
        c.get(f"{self.settings.repo_name}/pvss-setup.dat", '/tmp/pvss-setup.dat')
        nodes = max(bench_parameters.nodes)
        for i in range(nodes):
            c.get(f"{self.settings.repo_name}/pvss-sec{i}.conf", f'/tmp/pvss-sec{i}.conf')
            c.get(f"{self.settings.repo_name}/hotstuff-sec{i}.conf", f'/tmp/hotstuff-sec{i}.conf')

        # DONE: Push the setup to all the nodes
        names = range(nodes + bench_parameters.workers)
        progress = progress_bar(names, prefix='Uploading config files:')
        for i, name in enumerate(progress):
            for ip in hosts:
                c = Connection(ip, user='ubuntu', connect_kwargs=self.connect)
                c.run(f'{CommandMaker.cleanup()} || true', hide=True)
                c.put("/tmp/ip.txt", f'{self.settings.repo_name}/ip.txt')
                c.put('/tmp/pvss-setup.dat', f"{self.settings.repo_name}/pvss-setup.dat")
                for i in range(nodes):
                    c.put(f'/tmp/pvss-sec{i}.conf', f"{self.settings.repo_name}/pvss-sec{i}.conf")
                    c.put(f'/tmp/hotstuff-sec{i}.conf',f"{self.settings.repo_name}/hotstuff-sec{i}.conf")

    def _update(self, hosts):
        assert isinstance(hosts, list)
        for ip in hosts:
            assert isinstance(ip, str)

        Print.info(
            f'Updating {len(hosts)} machines (branch "{self.settings.branch}")...'
        )
        cmd = [
            f'(cd {self.settings.repo_name} && git fetch -f)',
            f'(cd {self.settings.repo_name} && git checkout -f {self.settings.branch})',
            f'(cd {self.settings.repo_name} && git pull -f)',
            f'(cd {self.settings.repo_name} && {CommandMaker.compile()})',
            CommandMaker.alias_binaries(
                f'./{self.settings.repo_name}'
            )
        ]
        g = Group(*hosts, user='ubuntu', connect_kwargs=self.connect)
        g.run(' && '.join(cmd), hide=True)

    def run_single(self, hosts, bench_parameters, nodes, workers):
        assert isinstance(bench_parameters, BenchParameters)
        assert isinstance(hosts, list)
        for ip in hosts:
            assert isinstance(ip, str)

        assert len(hosts) >= nodes+workers
        # Kill any potentially unfinished run and delete logs.
        self.kill(hosts=hosts, delete_logs=True)

        Print.info('Booting primaries...')
        for i, ip in enumerate(hosts[:nodes]):
            cmd = CommandMaker.run_primary(self.settings.repo_name, i)
            log_file = CommandMaker.log_file(self.settings.repo_name, i)
            self._background_run(ip, cmd, log_file)
        for ip in hosts[nodes:]:
            cmd = CommandMaker.run_client(self.settings.repo_name)
            log_file = CommandMaker.client_log_file(self.settings.repo_name)
            self._background_run(ip, cmd, log_file)

    def _background_run(self, ip, command, log_file):
        name = splitext(basename(log_file))[0]
        cmd = f'tmux new -d -s "{name}" "{command} |& tee {log_file}"'
        c = Connection(ip, user='ubuntu', connect_kwargs=self.connect)
        output = c.run(cmd, hide=True)
        self._check_stderr(output)

    def _logs(self, hosts, nodes, clients):
        assert isinstance(nodes, int)
        assert nodes > 0
        assert clients > 0
        assert isinstance(clients, int)
        assert isinstance(hosts, list)
        for ip in hosts:
            assert isinstance(ip, str)
        
        for i,ip in enumerate(hosts[:nodes]):
            c = Connection(ip, user='ubuntu', connect_kwargs=self.connect)
            log_file = CommandMaker.log_file(self.settings.repo_name, i)
            name = splitext(basename(log_file))[0]
            c.get(log_file, name)
        for ip in hosts[nodes:]:
            c = Connection(ip, user='ubuntu', connect_kwargs=self.connect)
            client_log_file = CommandMaker.client_log_file(self.settings.repo_name)
            name = splitext(basename(log_file))[0]
            c.get(client_log_file, name)

    def run_bench_optrand(self,bench_params_dict, debug=False):
        assert isinstance(debug, bool)
        Print.heading('Starting remote benchmark')
        try:
            bench_parameters = BenchParameters(bench_params_dict)
        except ConfigError as e:
            raise BenchError('Invalid nodes or bench parameters', e)

        # Select which hosts to use.
        selected_hosts = self._select_hosts(bench_parameters)
        if not selected_hosts:
            Print.warn('There are not enough instances available')
            return

        # Update nodes.
        try:
            self._update(selected_hosts)
        except (GroupException, ExecutionError) as e:
            e = FabricError(e) if isinstance(e, GroupException) else e
            raise BenchError('Failed to update nodes', e)

        # Upload all configuration files.
        try:
            self._config(
                selected_hosts, bench_parameters
            )
        except (subprocess.SubprocessError, GroupException) as e:
            e = FabricError(e) if isinstance(e, GroupException) else e
            raise BenchError('Failed to configure nodes', e)
        
        # Run the servers
        for n in bench_parameters.nodes:
            Print.heading(f'\nRunning {n} nodes')

            for i in range(bench_parameters.runs):
                Print.heading(f'Run {i+1}/{bench_parameters.runs}') 
                try:
                    self.run_single(selected_hosts, 
                                        bench_parameters, 
                                        n, 
                                        bench_parameters.workers)
                    sleep(60)
                    # Get the logs
                    self._logs(selected_hosts, n, bench_parameters.workers)
                    self.kill(hosts=selected_hosts)
                except (subprocess.SubprocessError, GroupException) as e:
                    self.kill(hosts=selected_hosts)
                    if isinstance(e, GroupException):
                        e = FabricError(e)
                    Print.error(BenchError('Benchmark failed', e))
                    continue 
