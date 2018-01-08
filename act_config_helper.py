#!/usr/bin/python
####
# Act config creator to help configuration for tests
####

import cmd
import os

class RunCommand(cmd.Cmd):
	__VERSION__ = "0.0.1"
	prompt = "ACT> "
	name = "Aerospike ACT config creator"

	def do_EOF(self, line):
		return True
	def do_exit(self, line):
		return True

	def do_createconfig(self,line):

		device_names = ''
		num_queues = 8
		threads_per_queue = 8
		test_duration_sec = 86400
		report_interval_sec = 1
		record_bytes = 1536
		large_block_op_kbytes = 128
		microsecond_histograms = 'no'
		scheduler_mode = 'noop'
		use_standard = True
		device_load = -1

		### Ask for details ###
		try:
			response = ''

			# Number of devices:
			while (not response.isdigit()):
				response = raw_input('Enter the number of devices you want to create config for: ')
				if response == '':
					continue
			no_of_devices = int(response)

			# Device names:
			print 'Enter either raw device if over-provisioned using hdparm or partition if over-provisioned using fdisk:'
			device_list = ''
			for device in range(1, no_of_devices + 1):
				response = ''
				while response == '':
					response = raw_input('Enter device name #' + str(device) + ' (e.g. /dev/sdb or /dev/sdb1): ')
					if not response == '':
						device_list += response
					if device != no_of_devices:
						device_list += ','
			device_names = device_list

			# Test duration:
			td = raw_input('Change test duration default of ' + str(test_duration_sec / 3600) + ' hours? (y/N): ')
			if 'y' == td or 'Y' == td:
				response = 'x'
				while (not response.isdigit()):
					response = raw_input('Enter the test duration in hours: ')
					if response == '':
						continue
				tds = int(response)
				test_duration_sec = tds * 3600

			# Non-standard items:
			response = raw_input('Use non-standard configuration? (y/N): ')
			if 'y' == response or 'Y' == response:

				# Read & write rates:
				rwops = raw_input('Configure read/write ops per second? (y/N): ')
				if 'y' == rwops or 'Y' == rwops:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter read ops per second for each device: ')
						if response == '':
							continue
					read_reqs_per_sec = int(response) * no_of_devices
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter write ops per second for each device: ')
						if response == '':
							continue
					write_reqs_per_sec = int(response) * no_of_devices

					use_standard = False

				# Record size:
				recsz = raw_input('Change record-bytes default of ' + str(record_bytes) + '? (y/N): ')
				if 'y' == recsz or 'Y' == recsz:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter record-bytes value: ')
						if response == '':
							continue
					record_bytes = response

				# Queues & threads:
				numq = raw_input('Change num-queues default of ' + str(num_queues) + '? (y/N): ')
				if 'y' == numq or 'Y' == numq:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter num-queues value: ')
						if response == '':
							continue
					num_queues = response
				tpq = raw_input('Change threads-per-queue default of ' + str(threads_per_queue) + '? (y/N): ')
				if 'y' == tpq or 'Y' == tpq:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter threads-per-queue value: ')
						if response == '':
							continue
					threads_per_queue = response

			# Load factor:
			if use_standard:
				response = 'x'
				while (not response.isdigit()):
					print '"1x" load is  2000 reads per second and 1000 writes per second.'
					response = raw_input('Enter the load factor (e.g. enter 1 for 1x test): ')
					if response == '':
						continue
				device_load = int(response)
				read_reqs_per_sec = no_of_devices * device_load * 2000
				write_reqs_per_sec = no_of_devices * device_load * 1000

			# Save file:
			response = ''
			while response == '':
				response = raw_input('Do you want to save this config to a file? (y/N): ')
				if 'y' == response or 'Y' == response:
					if device_load > 0:
						load_tag = str(device_load) + 'x'
						filename_load_tag = load_tag
					else:
						load_tag = 'non-standard'
						filename_load_tag = str(read_reqs_per_sec / no_of_devices) + 'r' + str(write_reqs_per_sec / no_of_devices) + 'w'
					actfile = 'actconfig_' + filename_load_tag + '_' + str(no_of_devices) + 'd.txt'
					try:
						act_file_fd = open(actfile, "wb")
						act_file_fd.write('##########\n')
						act_file_fd.write('act config file for testing ' + str(no_of_devices) + ' device(s) at ' + load_tag + ' load\n')
						act_file_fd.write('##########\n\n')
						act_file_fd.write('# comma-separated list:\n')
						act_file_fd.write('device-names: %s\n\n' % str(device_names))
						act_file_fd.write('# mandatory non-zero:\n')
						act_file_fd.write('num-queues: %s\n' % str(num_queues))
						act_file_fd.write('threads-per-queue: %s\n' % str(threads_per_queue))
						act_file_fd.write('test-duration-sec: %s\n' % str(test_duration_sec))
						act_file_fd.write('report-interval-sec: %s\n' % str(report_interval_sec))
						act_file_fd.write('large-block-op-kbytes: %s\n\n' % str(large_block_op_kbytes))
						act_file_fd.write('record-bytes: %s\n' % str(record_bytes))
						act_file_fd.write('read-reqs-per-sec: %s\n\n' % str(read_reqs_per_sec))
						act_file_fd.write('# usually non-zero:\n')
						act_file_fd.write('write-reqs-per-sec: %s\n' % str(write_reqs_per_sec))
						act_file_fd.write('# yes|no - default is no:\n')
						act_file_fd.write('microsecond-histograms: %s\n\n' % str(microsecond_histograms))
						act_file_fd.write('# noop|cfq - default is noop:\n')
						act_file_fd.write('scheduler-mode: %s\n' % str(scheduler_mode))
						act_file_fd.close()
						print 'Config file '+ str(actfile) + ' successfully created.'
					except Exception, i:
						print '\nException: ', i

		except Exception, i:
			print '\nException: ', i

if __name__ == '__main__':
	try:
		RunCommand().onecmd('createconfig')
	except (KeyboardInterrupt, SystemExit):
		print "getting error"
