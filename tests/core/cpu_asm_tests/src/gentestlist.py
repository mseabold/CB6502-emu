import argparse
import yaml

parser = argparse.ArgumentParser()

parser.add_argument('test_files', nargs='+')
parser.add_argument('-o','--output')

args = parser.parse_args()

entries = []
for f in args.test_files:
    test_name = f[4:]
    test_name = test_name[:len(test_name)-4]
    entry = {
            'name': test_name,
            'file': f
    }

    if "page" in test_name:
        entry['page_penalty'] = True

    if '_br' in test_name:
        entry['branch_penalty'] = True
    elif '_nobr' in test_name:
        entry['branch_penalty'] = False

    entries.append(entry)

if args.output:
    f = open(args.output, 'w+')
else:
    f = None
print(yaml.dump(entries),file=f)
