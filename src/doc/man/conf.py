import os

project = u'NFS-Ganesha'

def _get_description(fname, base):
    with open(fname) as f:
        one = None
        while True:
            line = f.readline().rstrip('\n')
            if not line:
                continue
            if line.startswith(':') and line.endswith(':'):
                continue
            one = line
            break
        two = f.readline().rstrip('\n')
        three = f.readline().rstrip('\n')
        assert one == three
        assert all(c=='=' for c in one)
        name, description = two.split('--', 1)
        assert name.strip() == base
        return description.strip()

def _get_manpages():
    man_dir = os.path.dirname(__file__)
    for filename in os.listdir(man_dir):
        base, ext = os.path.splitext(filename)
        if ext != '.rst':
            continue
        if base == 'index':
            continue
        path = os.path.join(man_dir, filename)
        description = _get_description(path, base)
        yield (
            os.path.join(base),
            base,
            description,
            '',
            8,
            )

man_pages = list(_get_manpages())

# sphinx warns if no toc is found, so feed it with a random file
# which is also rendered in this run.
master_doc = 'index'
