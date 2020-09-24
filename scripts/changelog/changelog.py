import enum

import click
import numpy as np
import pandas as pd


@enum.unique
class Area(enum.Enum):
    core = 'core'
    tests = 'tests'
    build = 'build'
    apps = 'apps'
    docs = 'docs'


def define_area(msg):
    areas = [e.value for e in Area]

    for area in areas:
        if msg.startswith(f'[{area}] '):
            return area

    return np.NaN


def delete_prefix(msg):
    prefixes = [f'[{e.value}] ' for e in Area]

    for prefix in prefixes:
        if msg.startswith(prefix):
            return msg[len(prefix):]

    return msg[:]


def write_into_changelog(df, f):
    f.write('\n')
    for _, row in df.iterrows():
        f.write(f"\n{row['commit']} {row['message']}")
    f.write('\n')


@click.command()
@click.argument(
    'git_log',
    type=click.Path(exists=True)
)
def main(git_log):
    """ Script designed to create changelog out of .csv SRT git log """

    df = pd.read_csv(git_log, sep = '|', names = ['commit', 'message', 'author', 'email'])
    df['area'] = df['message'].apply(define_area)
    df['message'] = df['message'].apply(delete_prefix)

    core = df[df['area']=='core']
    tests = df[df['area']=='tests']
    build = df[df['area']=='build']
    apps = df[df['area']=='apps']
    docs = df[df['area']=='docs']
    other = df[df['area'].isna()]

    with open('changelog.md', 'w') as f:
        f.write('# Release Notes\n')

        f.write('\n## Changelog\n')
        f.write('\n<details><summary>Click to expand/collapse</summary>')
        f.write('\n<p>')
        f.write('\n')

        if not core.empty:
            f.write('\n### Core Functionality')
            write_into_changelog(core, f)

        if not tests.empty:
            f.write('\n### Unit Tests')
            write_into_changelog(tests, f)

        if not build.empty:
            f.write('\n### Build Scripts (CMake, etc.)')
            write_into_changelog(build, f)

        if not apps.empty:
            f.write('\n### Sample Applications')
            write_into_changelog(apps, f)

        if not docs.empty:
            f.write('\n### Documentation')
            write_into_changelog(docs, f)

        if not other.empty:
            f.write('\n### Other')
            write_into_changelog(other, f)

        f.write('\n</p>')
        f.write('\n</details>')


if __name__ == '__main__':
    main()
