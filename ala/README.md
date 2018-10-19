## ACT Scripts


### Overview
------------
1. act_install.sh - install act on deb/el systems
2. act_make_configs.rb - create config files using ruby.
    * You will need to have ruby installed and run 'gem install optimist' for this to work.
3. runact.sh - run through a series of act configs and save the output.
    * This will start from 120x tests and continue down in steps of 10 to create act test results from 120 -> 10x based on the configs generated in act_make_configs.rb.
