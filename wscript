import os

srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')
  if 'LIBPATH_GMP' in os.environ:
    conf.env['LIBPATH_GMP'] = os.environ['LIBPATH_GMP']
  else:
    conf.env['LIBPATH_GMP'] = '/opt/local/lib'
  conf.env['LIBPATH_GMPXX'] = conf.env['LIBPATH_GMP']
  conf.env['LIB_GMP'] = 'gmp'
  conf.env['LIB_GMPXX'] = 'gmpxx'
  conf.link_add_flags();

def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.target = 'gmp'
  obj.source = 'node_gmp.cc'
  obj.uselib = ['GMP', 'GMPXX']
