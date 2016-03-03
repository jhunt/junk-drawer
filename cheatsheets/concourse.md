Concourse Cheatsheet
====================

Task
----

```yml
jobs:
  - name: do-stuff
    plan:
      - task: task-name
        config:
          platform: linux
          image: docker:///user/repo#tag
          inputs:
            - name: my-git-repo
              path: alt-path
          run:
            path: ./alt-path/ci/do-stuff
            args: [ foo, bar ]
          params:
            ENVIRONMENT: VARIABLE
```

Git Resource
------------

```yml
resources:
  - name: my-git-repo
    type: git
    source:
      uri: git@github.com/user/repo   # required
      branch: master                  # required
      private_key: ...
      ignore_paths: []
      skip_ssl_verification: false
      tag_filter: integr*te
```

Version Resource
----------------

```yml
resources:
  - name: my-version
    type: semver
    source:
      driver: s3 # optional, default
      key: version
      bucket: bucket-name
      access_key_id: ACCESS_KEY
      secret_access_key: SECRET_KEY
      initial_verison: 0.0.4
```
