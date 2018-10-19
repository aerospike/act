## ACT Scripts


### Overview
------------
1. act_install.sh - install act on deb/el systems
    * Usage: `sudo bash act_install.sh`
2. act_make_configs.rb - create config files using ruby.
    * You will need to have ruby installed and run 'gem install optimist' for this to work.
    * Usage: `apt install ruby || yum install ruby; gem install optimist; ruby act_make_configs.rb -d /dev/drive1p1,/dev/drive2p1`,etc... for help run without args or `ruby act_make_configs.rb -h`
3. runact.sh - run through a series of act configs and save the output.
    * This will start from 120x tests and continue down in steps of 10 to create act test results from 120 -> 10x based on the configs generated in act_make_configs.rb.
    * Usage: `bash runact.sh` default: 120x-10x tests. To modify the start/end of the act tests (must have configs present) use `bash runact.sh upto downto` ex.. `bash runact.sh 120 20` will run act tests from 120x to 20x.
