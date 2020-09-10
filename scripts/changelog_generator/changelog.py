import pandas as pd


def define_area(msg):
    areas = [
        'core',
        'API',
        'build',
        'tests',
        'apps',
        'docs',
    ]

    for area in areas:
        if msg.startswith(f'[{area}] '):
            return area

    return None


def delete_prefix(msg):
    prefixes = [
        '[core] ',
        '[API] ',
        '[build] ',
        '[tests] ',
        '[apps] ',
        '[docs] ',
    ]

    for prefix in prefixes:
        if msg.startswith(prefix):
            return msg[len(prefix):]

    return msg[:]


df = pd.read_csv('./commits.csv', sep = '|', names = ['commit', 'message', 'author', 'email'])
print(df)

df['area'] = df['message'].apply(define_area)
print(df)

df['message'] = df['message'].apply(delete_prefix)
print(df)