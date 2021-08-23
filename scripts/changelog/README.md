# Changelog

Script designed to create changelog out of `.csv` SRT git log. The output `changelog.md` is generated in the root folder.

In order to generate git log file since the previous release (e.g., v1.4.0), use the following command:

```
git log --pretty=format:"%h|%s|%an|%ae" v1.4.0...HEAD^ > commits.csv
```

## Requirements

* python 3.6+

To install python libraries use:
```
pip install -r requirements.txt
```
