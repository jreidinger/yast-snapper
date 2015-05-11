/*
 * SnapperAgent.cc
 *
 * An agent for accessing snapper library
 *
 * Authors: Jiri Suchomel <jsuchome@suse.cz>
 */

#include "SnapperAgent.h"

#define PC(n)       (path->component_str(n))

using namespace snapper;

/*
 * search the map for value of given key; both key and value have to be strings
 */
string SnapperAgent::getValue (const YCPMap &map, const YCPString &key, const string &deflt)
{
    YCPValue val = map->value(key);
    if (!val.isNull() && val->isString())
	return val->asString()->value();
    else
	return deflt;
}

/**
 * Search the map for value of given key
 * @param map YCP Map to look in
 * @param key key we are looking for
 * @param deflt the default value to be returned if key is not found
 */
int SnapperAgent::getIntValue (const YCPMap &map, const YCPString &key, const int deflt)
{
    YCPValue val = map->value(key);

    if (!val.isNull() && val->isInteger()) {
	return val->asInteger()->value();
    }
    else if (!val.isNull() && val->isString()) {
	return YCPInteger (val->asString()->value().c_str())->value ();
    }
    return deflt;
}


void
log_do(LogLevel level, const string& component, const char* file, const int line, const char* func,
       const string& text)
{
    static const loglevel_t ln[4] = { LOG_DEBUG, LOG_MILESTONE, LOG_WARNING, LOG_ERROR };

    y2_logger_function(ln[level], component, file, line, func, "%s", text.c_str());
}


bool
log_query(LogLevel level, const string& component)
{
    static const loglevel_t ln[4] = { LOG_DEBUG, LOG_MILESTONE, LOG_WARNING, LOG_ERROR };

    return should_be_logged(ln[level], component);
}


/**
 * Constructor
 */
SnapperAgent::SnapperAgent()
    : SCRAgent(), sh(NULL), snapper_initialized(false)
{
    snapper::setLogDo(&log_do);
    snapper::setLogQuery(&log_query);
}

/**
 * Destructor
 */
SnapperAgent::~SnapperAgent()
{
    delete sh;
}


/**
 * Dir
 */
YCPList SnapperAgent::Dir(const YCPPath& path)
{
    y2error("Wrong path '%s' in Read().", path->toString().c_str());
    return YCPNull();
}


struct Tree { map<string, Tree> trees; };

static YCPMap
make_ycpmap(const Tree& tree)
{
    YCPMap ret;

    for (map<string, Tree>::const_iterator it = tree.trees.begin(); it != tree.trees.end(); ++it)
	ret->add(YCPString(it->first), make_ycpmap(it->second));

    return ret;
}


/**
 * Read
 */
YCPValue SnapperAgent::Read(const YCPPath &path, const YCPValue& arg, const YCPValue& opt)
{
    y2debug ("path in Read: '%s'.", path->toString().c_str());
    YCPValue ret = YCPVoid();

    YCPMap argmap;
    if (!arg.isNull() && arg->isMap())
    	argmap = arg->asMap();

    if (!snapper_initialized && PC(0) != "error" && PC(0) != "configs" && PC(0) != "is_subvolume") {
	y2error ("snapper not initialized: use Execute (.snapper) first!");
	return YCPVoid();
    }

    if (path->length() == 1) {

	unsigned int num1	= getIntValue (argmap, YCPString ("from"), 0);
	unsigned int num2	= getIntValue (argmap, YCPString ("to"), 0);

	/**
	 * Read(.snapper.diff_index) -> show difference between snapshots num1 and num2 as one-level map:
	 * (mapping each file to its changes)
	 */
	if (PC(0) == "diff_index") {
	    YCPMap retmap;

	    const Snapshots& snapshots = sh->getSnapshots();
	    const Comparison comparison(sh, snapshots.find(num1), snapshots.find(num2));
	    const Files& files = comparison.getFiles();

	    for (Files::const_iterator it = files.begin(); it != files.end(); ++it)
	    {
		YCPMap file_map;
		file_map->add (YCPString ("status"), YCPString (statusToString (it->getPreToPostStatus())));
		file_map->add (YCPString ("full_path"), YCPString (it->getAbsolutePath (LOC_SYSTEM)));
		retmap->add (YCPString (it->getName()), file_map);
	    }
	    return retmap;
	}
	/**
	 * Read(.snapper.diff_tree) -> show difference between snapshots num1 and num2 as tree.
	 */
	else if (PC(0) == "diff_tree")
	{
	    Tree ret1;

	    const Snapshots& snapshots = sh->getSnapshots();
	    const Comparison comparison(sh, snapshots.find(num1), snapshots.find(num2));
	    const Files& files = comparison.getFiles();
	    for (Files::const_iterator it = files.begin(); it != files.end(); ++it)
	    {
		deque<string> parts;
		boost::split(parts, it->getName(), boost::is_any_of("/"));
		parts.pop_front();

		Tree* tmp = &ret1;
		for (deque<string>::const_iterator it = parts.begin(); it != parts.end(); ++it)
		{
		    map<string, Tree>::iterator pos = tmp->trees.find(*it);
		    if (pos == tmp->trees.end())
			pos = tmp->trees.insert(tmp->trees.begin(), make_pair(*it, Tree()));
		    tmp = &pos->second;
		}
	    }

	    return make_ycpmap(ret1);
	}
	else {
	    y2error("Wrong path '%s' in Read().", path->toString().c_str());
	}
    }
    else if (path->length() == 2) {

	    y2error("Wrong path '%s' in Read().", path->toString().c_str());
    }
    else {
	y2error("Wrong path '%s' in Read().", path->toString().c_str());
    }
    return YCPVoid();
}

/**
 * Write
 */
YCPBoolean SnapperAgent::Write(const YCPPath &path, const YCPValue& arg,
       const YCPValue& arg2)
{
    y2debug ("path in Write: '%s'.", path->toString().c_str());

    YCPBoolean ret = YCPBoolean(true);
    return ret;
}

/**
 * Execute
 */
YCPValue SnapperAgent::Execute(const YCPPath &path, const YCPValue& arg,
	const YCPValue& arg2)
{
    y2debug ("path in Execute: '%s'.", path->toString().c_str());
    YCPValue ret = YCPBoolean (true);

    YCPMap argmap;
    if (!arg.isNull() && arg->isMap())
    	argmap = arg->asMap();

    /**
     * Execute (.snapper) call: Initialize snapper object
     */
    if (path->length() == 0) {

	snapper_initialized	= false;
	if (sh)
	{
	    y2milestone ("deleting existing snapper object");
	    delete sh;
	    sh = 0;
	}
	string config_name = getValue (argmap, YCPString ("config"), "root");
	try
	{
	    sh = new Snapper(config_name, "/");
	}
	catch (const ConfigNotFoundException& e)
	{
	    y2error ("Config not found.");
	    sh			= NULL;
	    return YCPBoolean (false);
	}
	catch (const InvalidConfigException& e)
	{
	    y2error ("Config is invalid.");
	    sh			= NULL;
	    return YCPBoolean (false);
	}
	snapper_initialized	= true;
	return ret;
    }

    if (path->length() == 1) {

        // previous operations do not need initialization
        if (!snapper_initialized) {
          y2error ("snapper not initialized: use Execute (.snapper) first!");
          return YCPVoid();
        }

    }
    else {
	y2error("Wrong path '%s' in Execute().", path->toString().c_str());
    }

    return YCPVoid ();
}

/**
 * otherCommand
 */
YCPValue SnapperAgent::otherCommand(const YCPTerm& term)
{
    string sym = term->name();

    if (sym == "SnapperAgent") {
        /* Your initialization */
        return YCPVoid();
    }

    return YCPNull();
}
