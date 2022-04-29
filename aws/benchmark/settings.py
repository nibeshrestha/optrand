# class AwsAccessSettings:
#     aws_access_key_id = "AKIA6KNQANMVOOXSWHC3"
#     aws_secret_access_key = "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"

# class InstanceSettings:
#     type = "t3.micro"
#     regions = [
#         "us-east-1",        # N. Virginia
#         "us-east-2",        # Ohio
#         "us-west-1",        # N. California
#         "us-west-2",        # Oregon
#         "ca-central-1",     # Canada
#         "eu-west-1",        # Ireland
#         "ap-southeast-1",   # Singapore
#         "ap-northeast-1",   # Tokyo
#     ]


from json import load, JSONDecodeError

class SettingsError(Exception):
    pass

class Settings:
    def __init__(self, key_name, key_path, repo_name, repo_url,
                 branch, instance_type, aws_regions, aws_access, aws_secret):
        inputs_str = [
            key_name, key_path, repo_name, repo_url, branch, instance_type, aws_access, aws_secret
        ]
        if isinstance(aws_regions, list):
            regions = aws_regions
        else:
            regions = [aws_regions]
        inputs_str += regions
        ok = all(isinstance(x, str) for x in inputs_str)
        ok &= len(regions) > 0
        if not ok:
            raise SettingsError('Invalid settings types')

        self.key_name = key_name
        self.key_path = key_path

        self.repo_name = repo_name
        self.repo_url = repo_url
        self.branch = branch

        self.instance_type = instance_type
        self.aws_regions = regions

        self.aws_access_key_id = aws_access
        self.aws_secret_access_key = aws_secret

    @classmethod
    def load(cls, filename):
        try:
            with open(filename, 'r') as f:
                data = load(f)

            return cls(
                data['key']['name'],
                data['key']['path'],
                data['repo']['name'],
                data['repo']['url'],
                data['repo']['branch'],
                data['instances']['type'],
                data['instances']['regions'],
                data['aws']['access'],
                data['aws']['secret']
            )
        except (OSError, JSONDecodeError) as e:
            raise SettingsError(str(e))

        except KeyError as e:
            raise SettingsError(f'Malformed settings: missing key {e}')
