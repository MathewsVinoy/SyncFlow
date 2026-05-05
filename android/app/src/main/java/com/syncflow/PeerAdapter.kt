package com.syncflow

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.TextView

class PeerAdapter(
    private val context: Context,
    private val peers: MutableList<DiscoveredPeer>
) : BaseAdapter() {

    override fun getCount(): Int = peers.size

    override fun getItem(position: Int): DiscoveredPeer = peers[position]

    override fun getItemId(position: Int): Long = position.toLong()

    override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
        val view = convertView ?: LayoutInflater.from(context)
            .inflate(android.R.layout.simple_list_item_2, parent, false)

        val peer = peers[position]
        val title = view.findViewById<TextView>(android.R.id.text1)
        val subtitle = view.findViewById<TextView>(android.R.id.text2)

        title.text = peer.name
        subtitle.text = "${peer.ip}:${peer.port}"

        return view
    }

    fun updatePeers(newPeers: List<DiscoveredPeer>) {
        peers.clear()
        peers.addAll(newPeers)
        notifyDataSetChanged()
    }
}
