import paramiko
import sys

def ssh_exec(host="192.168.136.137", user="abc", password="abc", cmd="whoami"):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(hostname=host, username=user, password=password, port=22, timeout=10)
    stdin, stdout, stderr = client.exec_command(cmd, timeout=30)
    out = stdout.read().decode()
    err = stderr.read().decode()
    client.close()
    if out:
        print(out)
    if err:
        print(err, file=sys.stderr)
    return out

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "whoami"
    ssh_exec(cmd=cmd)
