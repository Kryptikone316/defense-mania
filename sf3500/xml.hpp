/* A tiny XML reader for the game's own data files (elements, attributes in ' or ", text; no entities). */
#ifndef DM_XML_HPP
#define DM_XML_HPP

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

struct XNode {
	std::string name, text;
	std::vector<std::pair<std::string, std::string> > attrs;
	std::vector<XNode> kids;

	std::string str(const char *key, const char *def = "") const
	{
		for (size_t i = 0; i < attrs.size(); i++)
			if (attrs[i].first == key)
				return attrs[i].second;
		return def;
	}
	int num(const char *key, int def = 0) const { std::string v = str(key, ""); return v.empty() ? def : atoi(v.c_str()); }
	float flt(const char *key, float def = 0) const { std::string v = str(key, ""); return v.empty() ? def : (float)atof(v.c_str()); }
	const XNode *child(const char *n) const
	{
		for (size_t i = 0; i < kids.size(); i++)
			if (kids[i].name == n)
				return &kids[i];
		return NULL;
	}
	/* "a/b/c" - the element list of the last step under the first matches of the earlier ones */
	std::vector<const XNode *> list(const std::string &path) const
	{
		std::vector<const XNode *> cur(1, this);
		size_t start = 0;
		while (start <= path.size())
		{
			size_t slash = path.find('/', start);
			std::string step = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
			std::vector<const XNode *> next;
			for (size_t i = 0; i < cur.size(); i++)
				for (size_t k = 0; k < cur[i]->kids.size(); k++)
					if (cur[i]->kids[k].name == step)
						next.push_back(&cur[i]->kids[k]);
			if (slash == std::string::npos)
				return next;
			cur.assign(1, next.empty() ? NULL : next[0]);
			if (!cur[0])
				return std::vector<const XNode *>();
			start = slash + 1;
		}
		return cur;
	}
};

static bool xml_parse_element(const std::string &s, size_t &i, XNode &node)
{
	/* at '<' of an element */
	i++;
	size_t j = i;
	while (j < s.size() && !isspace((unsigned char)s[j]) && s[j] != '>' && s[j] != '/')
		j++;
	node.name = s.substr(i, j - i);
	i = j;
	for (;;)
	{
		while (i < s.size() && isspace((unsigned char)s[i]))
			i++;
		if (i >= s.size())
			return false;
		if (s[i] == '/')
		{
			i += 2;   /* "/>" */
			return true;
		}
		if (s[i] == '>')
		{
			i++;
			break;
		}
		size_t e = i;
		while (e < s.size() && s[e] != '=' && !isspace((unsigned char)s[e]))
			e++;
		std::string key = s.substr(i, e - i);
		while (e < s.size() && (s[e] == '=' || isspace((unsigned char)s[e])))
			e++;
		char q = s[e];
		size_t v = e + 1, ve = s.find(q, v);
		if (ve == std::string::npos)
			return false;
		node.attrs.push_back(std::make_pair(key, s.substr(v, ve - v)));
		i = ve + 1;
	}
	/* content */
	for (;;)
	{
		size_t lt = s.find('<', i);
		if (lt == std::string::npos)
			return false;
		node.text += s.substr(i, lt - i);
		if (s.compare(lt, 2, "</") == 0)
		{
			i = s.find('>', lt) + 1;
			return true;
		}
		if (s.compare(lt, 4, "<!--") == 0)
		{
			i = s.find("-->", lt) + 3;
			continue;
		}
		XNode kid;
		i = lt;
		if (!xml_parse_element(s, i, kid))
			return false;
		node.kids.push_back(kid);
	}
}

static bool xml_load(const std::string &path, XNode &root)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::string s;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		s.append(buf, n);
	fclose(f);
	size_t i = 0;
	for (;;)
	{
		size_t lt = s.find('<', i);
		if (lt == std::string::npos)
			return false;
		if (s.compare(lt, 2, "<?") == 0)
			i = s.find("?>", lt) + 2;
		else if (s.compare(lt, 4, "<!--") == 0)
			i = s.find("-->", lt) + 3;
		else
		{
			i = lt;
			return xml_parse_element(s, i, root);
		}
	}
}

#endif
