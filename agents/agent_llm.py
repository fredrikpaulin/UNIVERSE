#!/usr/bin/env python3
"""Redirect — canonical location is agents/llm/agent.py"""
import runpy, sys, os
sys.argv[0] = os.path.join(os.path.dirname(__file__), "llm", "agent.py")
runpy.run_path(sys.argv[0], run_name="__main__")
